// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "virtio_graphics_driver.h"

#include <cstring>
#include <iostream>

#include "perception/cache.h"
#include "perception/debug.h"
#include "perception/interrupts.h"
#include "perception/memory.h"
#include "perception/multiboot.h"
#include "perception/pci.h"
#include "perception/port_io.h"
#include "perception/processes.h"

namespace graphics = ::perception::devices::graphics;
using ::perception::AllocateMemoryPages;
using ::perception::AllocateMemoryPagesBelowPhysicalAddressBase;
using ::perception::FlushRange;
using ::perception::GetMultibootFramebufferDetails;
using ::perception::GetPhysicalAddressOfVirtualAddress;
using ::perception::kMaxInterruptReadBytes;
using ::perception::kPageSize;
using ::perception::MapPhysicalMemory;
using ::perception::MessageId;
using ::perception::NotifyUponProcessTermination;
using ::perception::ProcessId;
using ::perception::Read16BitsFromPciConfig;
using ::perception::Read16BitsFromPort;
using ::perception::Read32BitsFromPciConfig;
using ::perception::Read32BitsFromPort;
using ::perception::Read8BitsFromPciConfig;
using ::perception::Read8BitsFromPort;
using ::perception::RegisterInterruptHandler;
using ::perception::RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort;
using ::perception::ReleaseMemoryPages;
using ::perception::SharedMemory;
using ::perception::StopNotifyingUponProcessTermination;
using ::perception::Write16BitsToPort;
using ::perception::Write32BitsToPort;
using ::perception::Write8BitsToPciConfig;
using ::perception::Write8BitsToPort;
using ::perception::devices::PciDevice;

namespace {

struct VirtioGpuCtrlHdr {
  uint32 type;
  uint32 flags;
  uint64 fence_id;
  uint32 ctx_id;
  uint32 padding;
} __attribute__((packed));

struct VirtioGpuRect {
  uint32 x;
  uint32 y;
  uint32 width;
  uint32 height;
} __attribute__((packed));

struct VirtioGpuGetDisplayInfo {
  VirtioGpuCtrlHdr hdr;
} __attribute__((packed));

struct VirtioGpuDisplayOne {
  VirtioGpuRect r;
  uint32 enabled;
  uint32 flags;
} __attribute__((packed));

struct VirtioGpuRespDisplayInfo {
  VirtioGpuCtrlHdr hdr;
  VirtioGpuDisplayOne pmodes[16];
} __attribute__((packed));

struct VirtioGpuResourceCreate2d {
  VirtioGpuCtrlHdr hdr;
  uint32 resource_id;
  uint32 format;
  uint32 width;
  uint32 height;
} __attribute__((packed));

struct VirtioGpuResourceAttachBacking {
  VirtioGpuCtrlHdr hdr;
  uint32 resource_id;
  uint32 nr_entries;
} __attribute__((packed));

struct VirtioGpuMemEntry {
  uint64 addr;
  uint32 length;
  uint32 padding;
} __attribute__((packed));

struct VirtioGpuSetScanout {
  VirtioGpuCtrlHdr hdr;
  VirtioGpuRect r;
  uint32 scanout_id;
  uint32 resource_id;
} __attribute__((packed));

struct VirtioGpuTransferToHost2d {
  VirtioGpuCtrlHdr hdr;
  VirtioGpuRect r;
  uint64 offset;
  uint32 resource_id;
  uint32 padding;
} __attribute__((packed));

struct VirtioGpuResourceFlush {
  VirtioGpuCtrlHdr hdr;
  VirtioGpuRect r;
  uint32 resource_id;
  uint32 padding;
} __attribute__((packed));

struct VirtioGpuResourceUnref {
  VirtioGpuCtrlHdr hdr;
  uint32 resource_id;
  uint32 padding;
} __attribute__((packed));

// This is kind of hacky and needs a better implementation.
void* AllocateContiguousMemoryPages(size_t pages, size_t& physical_address) {
  if (pages == 0) return nullptr;
  int attempts = 1;
  while (attempts-- > 0) {
    void* virt_addr = AllocateMemoryPagesBelowPhysicalAddressBase(
        pages, 0xFFFFFFFF, physical_address);
    if (!virt_addr) return nullptr;
    if (pages == 1) return virt_addr;

    size_t phys0 = GetPhysicalAddressOfVirtualAddress((size_t)virt_addr);
    bool contiguous = true;
    for (size_t i = 1; i < pages; i++) {
      if (GetPhysicalAddressOfVirtualAddress(
              (size_t)virt_addr + i * kPageSize) != phys0 + i * kPageSize) {
        contiguous = false;
        break;
      }
    }

    if (contiguous) {
      physical_address = phys0;
      return virt_addr;
    }

    ReleaseMemoryPages(virt_addr, pages);
  }
  return nullptr;
}

}  // namespace

VirtioGraphicsDriver::VirtioGraphicsDriver(const PciDevice& device)
    : GraphicsDevice::Server(),
      device_(device),
      common_cfg_(nullptr),
      notify_cfg_(nullptr),
      isr_cfg_(nullptr),
      notify_off_multiplier_(0),
      framebuffer_(nullptr),
      next_texture_id_(1),
      // Modern MMIO and Legacy I/O support
      process_allowed_to_write_to_the_screen_(0) {
  uint8 cmd_low =
      Read8BitsFromPciConfig(device.bus, device.slot, device.function, 0x04);
  uint8 cmd_high =
      Read8BitsFromPciConfig(device.bus, device.slot, device.function, 0x05);
  uint16 command = cmd_low | (cmd_high << 8);
  command |= 1 | 2 | 4;  // I/O space, Memory space, Bus Master
  command &= ~(1 << 10);
  Write8BitsToPciConfig(device.bus, device.slot, device.function, 0x04,
                        command & 0xFF);
  Write8BitsToPciConfig(device.bus, device.slot, device.function, 0x05,
                        (command >> 8) & 0xFF);

  uint64 bar_phys[6] = {0};
  uint16 io_port_base = 0;
  for (int i = 0; i < 6; i++) {
    uint32 bar = Read32BitsFromPciConfig(device.bus, device.slot,
                                         device.function, 0x10 + i * 4);
    if (bar != 0) {
      if ((bar & 1) != 0) {
        io_port_base = bar & 0xFFFC;
      } else {
        uint64 phys = bar & 0xFFFFFFF0;
        if ((bar & 4) != 0 && i + 1 < 6) {
          uint32 upper = Read32BitsFromPciConfig(
              device.bus, device.slot, device.function, 0x10 + (i + 1) * 4);
          phys |= ((uint64)upper << 32);
          bar_phys[i] = phys;
          bar_phys[i + 1] = phys;
          i++;
        } else {
          bar_phys[i] = phys;
        }
      }
    }
  }

  uint8 cap_ptr =
      Read8BitsFromPciConfig(device.bus, device.slot, device.function, 0x34);
  while (cap_ptr != 0) {
    uint8 cap_id = Read8BitsFromPciConfig(device.bus, device.slot,
                                          device.function, cap_ptr);
    if (cap_id == 0x09) {
      uint8 cfg_type = Read8BitsFromPciConfig(device.bus, device.slot,
                                              device.function, cap_ptr + 3);
      uint8 bar_idx = Read8BitsFromPciConfig(device.bus, device.slot,
                                             device.function, cap_ptr + 4);
      uint32 offset = Read32BitsFromPciConfig(device.bus, device.slot,
                                              device.function, cap_ptr + 8);
      uint32 length = Read32BitsFromPciConfig(device.bus, device.slot,
                                              device.function, cap_ptr + 12);

      if (bar_idx < 6 && bar_phys[bar_idx] != 0 && length > 0) {
        uint64 cap_phys = bar_phys[bar_idx] + offset;
        size_t page_offset = cap_phys & 4095;
        size_t pages = (length + page_offset + 4095) / 4096;
        if (pages == 0) pages = 1;
        void* mapped = MapPhysicalMemory(cap_phys & ~4095, pages);

        if (mapped != nullptr && (size_t)mapped != (size_t)-1) {
          volatile uint8* ptr = (volatile uint8*)mapped + page_offset;
          if (cfg_type == 1) {  // Common
            common_cfg_ = ptr;
          } else if (cfg_type == 2) {  // Notify
            notify_cfg_ = ptr;
            notify_off_multiplier_ = Read32BitsFromPciConfig(
                device.bus, device.slot, device.function, cap_ptr + 16);
          } else if (cfg_type == 3) {  // ISR
            isr_cfg_ = ptr;
          }
        }
      }
    }
    cap_ptr = Read8BitsFromPciConfig(device.bus, device.slot, device.function,
                                     cap_ptr + 1);
  }

  io_base_ = io_port_base;

  if (common_cfg_ != nullptr) {
    common_cfg_[20] = 0;      // Reset
    common_cfg_[20] = 1;      // ACKNOWLEDGE
    common_cfg_[20] = 1 | 2;  // DRIVER

    // Select feature word 1 (bits 32-63)
    *(volatile uint32*)(&common_cfg_[8]) = 1;
    // Accept VIRTIO_F_VERSION_1 (bit 32)
    *(volatile uint32*)(&common_cfg_[12]) = 1;

    common_cfg_[20] = 1 | 2 | 8;  // FEATURES_OK

    ctrl_queue_.SetupModern(0, common_cfg_);

    common_cfg_[20] = 1 | 2 | 8 | 4;  // DRIVER_OK
  } else if (io_base_ != 0) {
    Write8BitsToPort(io_base_ + 18, 0);  // Reset
    Write8BitsToPort(io_base_ + 18,
                     Read8BitsFromPort(io_base_ + 18) | 1);  // ACKNOWLEDGE
    Write8BitsToPort(io_base_ + 18,
                     Read8BitsFromPort(io_base_ + 18) | 2);  // DRIVER

    (void)Read32BitsFromPort(io_base_ + 0);
    Write32BitsToPort(io_base_ + 4, 0);

    ctrl_queue_.Setup(0, io_base_);

    Write8BitsToPort(io_base_ + 18,
                     Read8BitsFromPort(io_base_ + 18) | 4);  // DRIVER_OK
  } else {
    return;
  }

  if (!ctrl_queue_.avail || ctrl_queue_.size == 0) {
    ::perception::DebugPrinterSingleton
        << "ctrl_queue_ setup failed! avail=" << (size_t)ctrl_queue_.avail
        << " size=" << (size_t)ctrl_queue_.size << "\n";
    return;
  }

  VirtioGpuGetDisplayInfo get_display;
  memset(&get_display, 0, sizeof(get_display));
  get_display.hdr.type = 0x0100;

  VirtioGpuRespDisplayInfo disp_info;
  memset(&disp_info, 0, sizeof(disp_info));

  size_t physical_address = 0;
  uint32 mb_width = 0, mb_height = 0, pitch = 0;
  uint8 bpp = 0;
  GetMultibootFramebufferDetails(physical_address, mb_width, mb_height, pitch,
                                 bpp);

  if (mb_width > 0 && mb_height > 0) {
    screen_width_ = mb_width;
    screen_height_ = mb_height;
  } else {
    screen_width_ = 800;
    screen_height_ = 600;
  }

  (void)SendCommand(&get_display, sizeof(get_display), &disp_info,
                    sizeof(disp_info));

  CreateScanoutResource();

  uint8 irq =
      Read8BitsFromPciConfig(device.bus, device.slot, device.function, 0x3C);
  if (isr_cfg_ != nullptr) {
    RegisterInterruptHandler(irq, [this]() { HandleInterrupt(); });
  } else if (io_base_ != 0) {
    RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort(
        irq, io_base_ + 19, 3, io_base_ + 19, [this](const uint8* bytes) {
          for (int i = 0; i < kMaxInterruptReadBytes; i += 2) {
            uint8 status = bytes[i];
            if (status == 0) break;
            if ((status & 2) != 0) {
              ::perception::Defer([this]() { CheckForResolutionChange(); });
            }
          }
        });
  }
}

VirtioGraphicsDriver::~VirtioGraphicsDriver() {}

Status VirtioGraphicsDriver::RunCommands(const graphics::Commands& commands,
                                         ProcessId sender) {
  RenderState render_state;
  bool screen_modified = false;
  for (const auto& command : commands.commands) {
    RunCommand(command, sender, render_state, screen_modified);
  }
  if (screen_modified) {
    FlushScreen();
  }
  return Status::OK;
}

StatusOr<graphics::CreateTextureResponse> VirtioGraphicsDriver::CreateTexture(
    const graphics::CreateTextureRequest& request, ProcessId sender) {
  uint32 texture_id = next_texture_id_++;
  Texture texture;
  texture.owner = sender;
  texture.width = request.size.width;
  texture.height = request.size.height;
  texture.shared_memory = SharedMemory::FromSize(
      texture.width * texture.height * 4, SharedMemory::kJoinersCanWrite);

  auto process_information_itr = process_information_.find(sender);
  if (process_information_itr == process_information_.end()) {
    ProcessInformation process_information;
    process_information.on_process_disappear_listener =
        NotifyUponProcessTermination(sender, [this, sender]() {
          ReleaseAllResourcesBelongingToProcess(sender);
        });
    process_information.textures.insert(texture_id);
    process_information_[sender] = process_information;
  } else {
    process_information_itr->second.textures.insert(texture_id);
  }

  graphics::CreateTextureResponse response;
  response.texture.id = texture_id;
  response.pixel_buffer = texture.shared_memory;

  textures_[texture_id] = std::move(texture);

  return response;
}

Status VirtioGraphicsDriver::DestroyTexture(
    const graphics::TextureReference& request, ProcessId sender) {
  auto texture_itr = textures_.find(request.id);
  if (texture_itr == textures_.end()) return Status::INVALID_ARGUMENT;

  if (texture_itr->second.owner != sender) return Status::NOT_ALLOWED;

  textures_.erase(texture_itr);

  auto process_information_itr = process_information_.find(sender);
  if (process_information_itr == process_information_.end()) {
    return Status::INVALID_ARGUMENT;
  }

  process_information_itr->second.textures.erase(request.id);
  if (process_information_itr->second.textures.empty()) {
    StopNotifyingUponProcessTermination(
        process_information_itr->second.on_process_disappear_listener);
    process_information_.erase(process_information_itr);
  }

  return Status::OK;
}

StatusOr<graphics::TextureInformation>
VirtioGraphicsDriver::GetTextureInformation(
    const graphics::TextureReference& request) {
  auto texture_itr = textures_.find(request.id);
  if (texture_itr == textures_.end()) return Status::INVALID_ARGUMENT;

  graphics::TextureInformation response;
  response.owner = texture_itr->second.owner;
  response.size.width = texture_itr->second.width;
  response.size.height = texture_itr->second.height;
  return response;
}

Status VirtioGraphicsDriver::SetProcessAllowedToDrawToScreen(
    const graphics::ProcessAllowedToDrawToScreenParameters& request) {
  process_allowed_to_write_to_the_screen_ = request.process;
  return Status::OK;
}

StatusOr<graphics::Size> VirtioGraphicsDriver::GetScreenSize() {
  graphics::Size response;
  response.width = screen_width_;
  response.height = screen_height_;
  return response;
}

void VirtioGraphicsDriver::RunCommand(const graphics::Command& graphics_command,
                                      ProcessId sender,
                                      RenderState& render_state,
                                      bool& screen_modified) {
  switch (graphics_command.type) {
    case graphics::Command::Type::SET_DESTINATION_TEXTURE:
      if (graphics_command.texture_reference) {
        SetDestinationTexture(sender, graphics_command.texture_reference->id,
                              render_state);
      }
      break;
    case graphics::Command::Type::SET_SOURCE_TEXTURE:
      if (graphics_command.texture_reference) {
        SetSourceTexture(graphics_command.texture_reference->id, render_state);
      }
      break;
    case graphics::Command::Type::FILL_RECTANGLE: {
      if (graphics_command.fill_rectangle_parameters) {
        const auto& parameters = *graphics_command.fill_rectangle_parameters;
        FillRectangle(parameters.destination.left, parameters.destination.top,
                      parameters.destination.left + parameters.size.width,
                      parameters.destination.top + parameters.size.height,
                      parameters.color, render_state);
        if (render_state.destination_texture &&
            render_state.destination_texture->owner == 0) {
          screen_modified = true;
        }
      }
      break;
    }
    case graphics::Command::Type::COPY_ENTIRE_TEXTURE: {
      BitBlt(sender, render_state, 0, 0, 0, 0, UINT_MAX, UINT_MAX, false);
      if (render_state.destination_texture &&
          render_state.destination_texture->owner == 0) {
        screen_modified = true;
      }
      break;
    }
    case graphics::Command::Type::COPY_ENTIRE_TEXTURE_WITH_ALPHA_BLENDING: {
      BitBlt(sender, render_state, 0, 0, 0, 0, UINT_MAX, UINT_MAX, true);
      if (render_state.destination_texture &&
          render_state.destination_texture->owner == 0) {
        screen_modified = true;
      }
      break;
    }
    case graphics::Command::Type::COPY_TEXTURE_TO_POSITION: {
      if (graphics_command.position) {
        const auto& position = *graphics_command.position;
        BitBlt(sender, render_state, 0, 0, position.left, position.top,
               UINT_MAX, UINT_MAX, false);
        if (render_state.destination_texture &&
            render_state.destination_texture->owner == 0) {
          screen_modified = true;
        }
      }
      break;
    }
    case graphics::Command::Type::
        COPY_TEXTURE_TO_POSITION_WITH_ALPHA_BLENDING: {
      if (graphics_command.position) {
        const auto& position = *graphics_command.position;
        BitBlt(sender, render_state, 0, 0, position.left, position.top,
               UINT_MAX, UINT_MAX, true);
        if (render_state.destination_texture &&
            render_state.destination_texture->owner == 0) {
          screen_modified = true;
        }
      }
      break;
    }
    case graphics::Command::Type::COPY_PART_OF_A_TEXTURE: {
      if (graphics_command.copy_part_of_texture_parameters) {
        const auto& parameters =
            *graphics_command.copy_part_of_texture_parameters;
        BitBlt(sender, render_state, parameters.source.left,
               parameters.source.top, parameters.destination.left,
               parameters.destination.top, parameters.size.width,
               parameters.size.height, false);
        if (render_state.destination_texture &&
            render_state.destination_texture->owner == 0) {
          screen_modified = true;
        }
      }
      break;
    }
    case graphics::Command::Type::COPY_PART_OF_A_TEXTURE_WITH_ALPHA_BLENDING: {
      if (graphics_command.copy_part_of_texture_parameters) {
        const auto& parameters =
            *graphics_command.copy_part_of_texture_parameters;
        BitBlt(sender, render_state, parameters.source.left,
               parameters.source.top, parameters.destination.left,
               parameters.destination.top, parameters.size.width,
               parameters.size.height, true);
        if (render_state.destination_texture &&
            render_state.destination_texture->owner == 0) {
          screen_modified = true;
        }
      }
      break;
    }
  }
}

void VirtioGraphicsDriver::SetDestinationTexture(ProcessId sender,
                                                 uint64 texture_id,
                                                 RenderState& render_state) {
  auto texture_itr = textures_.find(texture_id);
  if (texture_itr == textures_.end()) {
    render_state.destination_texture = nullptr;
  } else {
    if (texture_itr->second.owner == 0) {
      if (sender != process_allowed_to_write_to_the_screen_) {
        render_state.destination_texture = nullptr;
        return;
      }
    } else if (texture_itr->second.owner != sender) {
      render_state.destination_texture = nullptr;
      return;
    }
    render_state.destination_texture = &texture_itr->second;
  }
}

void VirtioGraphicsDriver::SetSourceTexture(uint64 texture_id,
                                            RenderState& render_state) {
  if (texture_id == 0) {
    render_state.source_texture = nullptr;
    return;
  }
  auto texture_itr = textures_.find(texture_id);
  if (texture_itr == textures_.end()) {
    render_state.source_texture = nullptr;
  } else {
    render_state.source_texture = &texture_itr->second;
  }
}

void VirtioGraphicsDriver::BitBlt(ProcessId sender,
                                  const RenderState& render_state,
                                  uint32 left_source, uint32 top_source,
                                  uint32 left_destination,
                                  uint32 top_destination, uint32 width_to_copy,
                                  uint32 height_to_copy, bool alpha_blend) {
  if (render_state.source_texture == nullptr ||
      render_state.destination_texture == nullptr) {
    return;
  }

  uint8* dest_ptr = nullptr;
  uint32 dest_width = render_state.destination_texture->width;
  uint32 dest_height = render_state.destination_texture->height;
  uint32 dest_pitch = dest_width * 4;

  if (render_state.destination_texture->owner == 0) {
    if (alpha_blend) return;
    dest_ptr = framebuffer_;
  } else {
    dest_ptr = (uint8*)**render_state.destination_texture->shared_memory;
  }

  BitBltToBuffer((uint8*)**render_state.source_texture->shared_memory,
                 render_state.source_texture->width,
                 render_state.source_texture->height, dest_ptr, dest_width,
                 dest_height, dest_pitch, left_source, top_source,
                 left_destination, top_destination, width_to_copy,
                 height_to_copy, alpha_blend);
}

void VirtioGraphicsDriver::BitBltToBuffer(
    uint8* source, uint32 source_width, uint32 source_height,
    uint8* destination, uint32 destination_width, uint32 destination_height,
    uint32 destination_pitch, uint32 left_source, uint32 top_source,
    uint32 left_destination, uint32 top_destination, uint32 width_to_copy,
    uint32 height_to_copy, bool alpha_blend) {
  if (top_source >= source_height || left_source >= source_width ||
      top_destination >= destination_height ||
      left_destination >= destination_width) {
    return;
  }

  if (top_source + height_to_copy > source_height)
    height_to_copy = source_height - top_source;
  if (top_destination + height_to_copy > destination_height)
    height_to_copy = destination_height - top_destination;
  if (left_source + width_to_copy > source_width)
    width_to_copy = source_width - left_source;
  if (left_destination + width_to_copy > destination_width)
    width_to_copy = destination_width - left_destination;

  if (width_to_copy == 0 || height_to_copy == 0) return;

  uint8* source_copy_start =
      &source[(top_source * source_width + left_source) * 4];
  uint8* destination_copy_start =
      &destination[top_destination * destination_pitch + left_destination * 4];

  for (uint32 y = 0; y < height_to_copy; y++) {
    uint8* src = source_copy_start;
    uint8* dest = destination_copy_start;

    for (uint32 x = 0; x < width_to_copy; x++) {
      if (!alpha_blend || src[3] == 0xFF) {
        *(uint32*)dest = *(uint32*)src;
      } else if (src[3] > 0) {
        int alpha = src[3];
        int inv_alpha = 255 - src[3];

        dest[0] =
            (uint8)((alpha * (int)src[0] + inv_alpha * (int)dest[0]) >> 8);
        dest[1] =
            (uint8)((alpha * (int)src[1] + inv_alpha * (int)dest[1]) >> 8);
        dest[2] =
            (uint8)((alpha * (int)src[2] + inv_alpha * (int)dest[2]) >> 8);
      }
      dest += 4;
      src += 4;
    }

    source_copy_start += source_width * 4;
    destination_copy_start += destination_pitch;
  }
}

void VirtioGraphicsDriver::FillRectangle(uint32 left, uint32 top, uint32 right,
                                         uint32 bottom, uint32 color,
                                         RenderState& render_state) {
  uint8* color_channels = (uint8*)&color;
  if (color_channels[3] == 0) return;

  if (render_state.destination_texture == nullptr) return;

  uint8* dest_ptr = nullptr;
  uint32 dest_width = render_state.destination_texture->width;
  uint32 dest_height = render_state.destination_texture->height;
  uint32 dest_pitch = dest_width * 4;

  bool to_framebuffer = (render_state.destination_texture->owner == 0);
  if (to_framebuffer) {
    dest_ptr = framebuffer_;
  } else {
    dest_ptr = (uint8*)**render_state.destination_texture->shared_memory;
  }

  left = std::max((uint32)0, left);
  top = std::max((uint32)0, top);
  right = std::min(right, dest_width);
  bottom = std::min(bottom, dest_height);

  if (right <= left || bottom <= top) return;

  if (color_channels[3] == 0xFF || to_framebuffer) {
    uint8* destination_copy_start = &dest_ptr[top * dest_pitch + left * 4];
    for (uint32 y = top; y < bottom; y++) {
      uint32* dest = (uint32*)destination_copy_start;
      for (uint32 x = left; x < right; x++) *dest++ = color;
      destination_copy_start += dest_pitch;
    }
  } else {
    int alpha = color_channels[3];
    int inv_alpha = 255 - color_channels[3];

    uint8* destination_copy_start = &dest_ptr[top * dest_pitch + left * 4];
    for (uint32 y = top; y < bottom; y++) {
      uint8* dest = destination_copy_start;
      for (uint32 x = left; x < right; x++) {
        dest[0] = (uint8)((alpha * (int)color_channels[0] +
                           inv_alpha * (int)dest[0]) >>
                          8);
        dest[1] = (uint8)((alpha * (int)color_channels[1] +
                           inv_alpha * (int)dest[1]) >>
                          8);
        dest[2] = (uint8)((alpha * (int)color_channels[2] +
                           inv_alpha * (int)dest[2]) >>
                          8);
        dest += 4;
      }
      destination_copy_start += dest_pitch;
    }
  }
}

void VirtioGraphicsDriver::ReleaseAllResourcesBelongingToProcess(
    ProcessId process) {
  auto process_information_itr = process_information_.find(process);
  if (process_information_itr == process_information_.end()) return;

  for (uint64 texture : process_information_itr->second.textures) {
    auto texture_itr = textures_.find(texture);
    if (texture_itr != textures_.end()) textures_.erase(texture_itr);
  }
  process_information_.erase(process_information_itr);
}

void VirtioGraphicsDriver::FlushScreen() {
  FlushRange(framebuffer_, screen_width_ * screen_height_ * 4);

  VirtioGpuTransferToHost2d transfer;
  memset(&transfer, 0, sizeof(transfer));
  transfer.hdr.type = 0x0105;  // VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D
  transfer.r.x = 0;
  transfer.r.y = 0;
  transfer.r.width = screen_width_;
  transfer.r.height = screen_height_;
  transfer.offset = 0;
  transfer.resource_id = 1;

  VirtioGpuCtrlHdr resp_hdr;
  SendCommand(&transfer, sizeof(transfer), &resp_hdr, sizeof(resp_hdr));

  VirtioGpuResourceFlush flush;
  memset(&flush, 0, sizeof(flush));
  flush.hdr.type = 0x0104;  // VIRTIO_GPU_CMD_RESOURCE_FLUSH
  flush.r.x = 0;
  flush.r.y = 0;
  flush.r.width = screen_width_;
  flush.r.height = screen_height_;
  flush.resource_id = 1;

  SendCommand(&flush, sizeof(flush), &resp_hdr, sizeof(resp_hdr));
}

void VirtioGraphicsDriver::CreateScanoutResource() {
  VirtioGpuResourceCreate2d create_2d;
  memset(&create_2d, 0, sizeof(create_2d));
  create_2d.hdr.type = 0x0101;
  create_2d.resource_id = 1;
  create_2d.format = 67;  // VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM
  create_2d.width = screen_width_;
  create_2d.height = screen_height_;

  VirtioGpuCtrlHdr resp_hdr;
  SendCommand(&create_2d, sizeof(create_2d), &resp_hdr, sizeof(resp_hdr));

  size_t pages =
      (screen_width_ * screen_height_ * 4 + kPageSize - 1) / kPageSize;
  framebuffer_ = (uint8*)AllocateMemoryPages(pages);
  uint32* fb_pixels = (uint32*)framebuffer_;
  for (size_t i = 0; i < screen_width_ * screen_height_; i++) {
    fb_pixels[i] = 0xFF000000;
  }

  size_t nr_entries = pages;
  size_t attach_cmd_size = sizeof(VirtioGpuResourceAttachBacking) +
                           nr_entries * sizeof(VirtioGpuMemEntry);
  size_t attach_pages = (attach_cmd_size + kPageSize - 1) / kPageSize;
  void* attach_buf = AllocateMemoryPages(attach_pages);

  if (attach_buf) {
    memset(attach_buf, 0, attach_pages * kPageSize);
    auto* attach_cmd = (VirtioGpuResourceAttachBacking*)attach_buf;
    attach_cmd->hdr.type = 0x0106;
    attach_cmd->resource_id = 1;
    attach_cmd->nr_entries = nr_entries;

    auto* entries = (VirtioGpuMemEntry*)(attach_cmd + 1);
    for (size_t i = 0; i < pages; i++) {
      entries[i].addr = GetPhysicalAddressOfVirtualAddress(
          (size_t)framebuffer_ + i * kPageSize);
      entries[i].length = kPageSize;
      entries[i].padding = 0;
    }

    SendCommand(attach_buf, attach_cmd_size, &resp_hdr, sizeof(resp_hdr));
    ReleaseMemoryPages(attach_buf, attach_pages);
  }

  VirtioGpuSetScanout set_scanout;
  memset(&set_scanout, 0, sizeof(set_scanout));
  set_scanout.hdr.type = 0x0103;
  set_scanout.r.x = 0;
  set_scanout.r.y = 0;
  set_scanout.r.width = screen_width_;
  set_scanout.r.height = screen_height_;
  set_scanout.scanout_id = 0;
  set_scanout.resource_id = 1;

  SendCommand(&set_scanout, sizeof(set_scanout), &resp_hdr, sizeof(resp_hdr));

  Texture texture;
  texture.owner = 0;
  texture.width = screen_width_;
  texture.height = screen_height_;
  textures_[0] = std::move(texture);

  FlushScreen();
}

void VirtioGraphicsDriver::HandleInterrupt() {
  uint8 isr = 0;
  if (isr_cfg_ != nullptr) {
    isr = isr_cfg_[0];
  } else if (io_base_ != 0) {
    isr = Read8BitsFromPort(io_base_ + 19);
  }

  if (isr != 0) {
    ::perception::Defer([this]() { CheckForResolutionChange(); });
  }
}

void VirtioGraphicsDriver::CheckForResolutionChange() {
  VirtioGpuGetDisplayInfo get_display;
  memset(&get_display, 0, sizeof(get_display));
  get_display.hdr.type = 0x0100;

  VirtioGpuRespDisplayInfo disp_info;
  memset(&disp_info, 0, sizeof(disp_info));

  if (SendCommand(&get_display, sizeof(get_display), &disp_info,
                  sizeof(disp_info))) {
    if (disp_info.hdr.type == 0x1101) {
      for (int i = 0; i < 16; i++) {
        if (disp_info.pmodes[i].enabled && disp_info.pmodes[i].r.width > 0 &&
            disp_info.pmodes[i].r.height > 0) {
          uint32 new_width = disp_info.pmodes[i].r.width;
          uint32 new_height = disp_info.pmodes[i].r.height;
          if (new_width != screen_width_ || new_height != screen_height_) {
            if (framebuffer_) {
              VirtioGpuSetScanout disable_scanout;
              memset(&disable_scanout, 0, sizeof(disable_scanout));
              disable_scanout.hdr.type = 0x0103;
              disable_scanout.scanout_id = 0;
              disable_scanout.resource_id = 0;
              VirtioGpuCtrlHdr resp_hdr;
              SendCommand(&disable_scanout, sizeof(disable_scanout), &resp_hdr,
                          sizeof(resp_hdr));

              VirtioGpuResourceUnref unref;
              memset(&unref, 0, sizeof(unref));
              unref.hdr.type = 0x0102;
              unref.resource_id = 1;
              SendCommand(&unref, sizeof(unref), &resp_hdr, sizeof(resp_hdr));

              size_t old_pages =
                  (screen_width_ * screen_height_ * 4 + kPageSize - 1) /
                  kPageSize;
              ReleaseMemoryPages(framebuffer_, old_pages);
              framebuffer_ = nullptr;
            }

            screen_width_ = new_width;
            screen_height_ = new_height;
            CreateScanoutResource();

            if (graphics_listener_ && graphics_listener_->IsValid()) {
              graphics::Size new_size;
              new_size.width = screen_width_;
              new_size.height = screen_height_;
              graphics_listener_->ScreenSizeChanged(new_size, nullptr);
            }
          }
          break;
        }
      }
    }
  }
}

Status VirtioGraphicsDriver::SetGraphicsListener(
    const perception::devices::GraphicsListener::Client& listener) {
  if (listener.IsValid()) {
    graphics_listener_ =
        std::make_unique<perception::devices::GraphicsListener::Client>(
            listener);
  } else {
    graphics_listener_.reset();
  }
  return Status::OK;
}

bool VirtioGraphicsDriver::SendCommand(const void* req_data, size_t req_len,
                                       void* resp_data, size_t resp_len) {
  std::lock_guard<std::mutex> lock(ctrl_mutex_);
  if (!ctrl_queue_.avail || ctrl_queue_.size == 0) return false;

  if (resp_len > 4096) return false;

  uint16 head_idx = ctrl_queue_.next_desc;
  uint16 cur_idx = head_idx;

  if (req_len <= 4096) {
    uint8* req_buf = (uint8*)ctrl_queue_.buffers_virt[cur_idx];
    memcpy(req_buf, req_data, req_len);
    FlushRange(req_buf, req_len);
    ctrl_queue_.desc[cur_idx].addr = ctrl_queue_.buffers_phys[cur_idx];
    ctrl_queue_.desc[cur_idx].len = req_len;
    ctrl_queue_.desc[cur_idx].flags = 1;  // VRING_DESC_F_NEXT
    uint16 next_idx = (cur_idx + 1) % ctrl_queue_.size;
    ctrl_queue_.desc[cur_idx].next = next_idx;
    cur_idx = next_idx;
  } else {
    size_t pages = (req_len + kPageSize - 1) / kPageSize;
    if (pages == 0) pages = 1;

    if (pages + 1 > ctrl_queue_.size) return false;

    FlushRange((void*)req_data, req_len);

    for (size_t p = 0; p < pages; p++) {
      size_t phys =
          GetPhysicalAddressOfVirtualAddress((size_t)req_data + p * kPageSize);
      ctrl_queue_.desc[cur_idx].addr = phys;
      ctrl_queue_.desc[cur_idx].len =
          (p == pages - 1) ? (req_len - p * kPageSize) : kPageSize;
      ctrl_queue_.desc[cur_idx].flags = 1;  // VRING_DESC_F_NEXT
      uint16 next_idx = (cur_idx + 1) % ctrl_queue_.size;
      ctrl_queue_.desc[cur_idx].next = next_idx;
      cur_idx = next_idx;
    }
  }

  uint16 resp_idx = cur_idx;
  ctrl_queue_.desc[resp_idx].addr = ctrl_queue_.buffers_phys[resp_idx];
  ctrl_queue_.desc[resp_idx].len = resp_len;
  ctrl_queue_.desc[resp_idx].flags = 2;  // VRING_DESC_F_WRITE
  ctrl_queue_.desc[resp_idx].next = 0;

  ctrl_queue_.next_desc = (resp_idx + 1) % ctrl_queue_.size;

  uint8* resp_buf = (uint8*)ctrl_queue_.buffers_virt[resp_idx];
  memset(resp_buf, 0, resp_len);

  ctrl_queue_.avail->ring[ctrl_queue_.avail->idx % ctrl_queue_.size] = head_idx;

  FlushRange(ctrl_queue_.desc, 4096);
  FlushRange(ctrl_queue_.avail, 4096);
  FlushRange(ctrl_queue_.used, 4096);
  FlushRange(resp_buf, resp_len);

  __asm__ __volatile__("" ::: "memory");
  ctrl_queue_.avail->idx++;
  __asm__ __volatile__("" ::: "memory");

  FlushRange(ctrl_queue_.avail, 4096);

  if (common_cfg_ != nullptr && notify_cfg_ != nullptr) {
    uint32 noff = ctrl_queue_.notify_off * notify_off_multiplier_;
    *(volatile uint16*)(notify_cfg_ + noff) = 0;
    FlushRange((void*)(notify_cfg_ + noff), 4);
  } else if (io_base_ != 0) {
    Write16BitsToPort(io_base_ + 16, 0);
  }

  int timeout = 50000000;
  while (ctrl_queue_.last_seen_used == ctrl_queue_.used->idx) {
    if (isr_cfg_) {
      volatile uint8 isr = isr_cfg_[0];
    }
    FlushRange(ctrl_queue_.used, 4096);
    timeout--;
    if (timeout <= 0) return false;
  }

  ctrl_queue_.last_seen_used = ctrl_queue_.used->idx;

  FlushRange(resp_buf, resp_len);

  if (resp_data && resp_len > 0) memcpy(resp_data, resp_buf, resp_len);

  return true;
}

void VirtioGraphicsDriver::QueueDetails::Setup(uint16 queue_idx,
                                               uint16 io_base) {
  Write16BitsToPort(io_base + 14, queue_idx);
  uint16 qsize = Read16BitsFromPort(io_base + 12);
  if (qsize == 0) return;

  size_t desc_table_size = qsize * 16;
  size_t avail_ring_size = 6 + qsize * 2;
  size_t used_ring_offset = (desc_table_size + avail_ring_size + 4095) & ~4095;
  size_t used_ring_size = 6 + qsize * 8;
  size_t total_size = used_ring_offset + used_ring_size;
  size_t pages = (total_size + 4095) / 4096;

  size_t physical_address = 0;
  void* virt_addr = AllocateContiguousMemoryPages(pages, physical_address);
  if (!virt_addr) return;

  memset(virt_addr, 0, pages * 4096);

  size = qsize;
  mem = virt_addr;
  phys = physical_address;
  last_seen_used = 0;
  next_desc = 0;

  desc = (VirtQueueDesc*)virt_addr;
  avail = (VirtQueueAvail*)((size_t)virt_addr + desc_table_size);
  used = (VirtQueueUsed*)((size_t)virt_addr + used_ring_offset);

  for (int i = 0; i < qsize; i++) {
    buffers_virt[i] = AllocateMemoryPages(1);
    buffers_phys[i] =
        GetPhysicalAddressOfVirtualAddress((size_t)buffers_virt[i]);
  }

  avail->flags = 1;
  avail->idx = 0;

  FlushRange(virt_addr, pages * 4096);
  Write32BitsToPort(io_base + 8, physical_address / 4096);
}

void VirtioGraphicsDriver::QueueDetails::SetupModern(
    uint16 queue_idx, volatile uint8* common_cfg) {
  *(volatile uint16*)(&common_cfg[22]) = queue_idx;
  uint16 qsize = *(volatile uint16*)(&common_cfg[24]);
  if (qsize == 0) return;

  *(volatile uint16*)(&common_cfg[24]) = qsize;

  notify_off = *(volatile uint16*)(&common_cfg[30]);

  void* desc_virt = AllocateMemoryPages(1);
  void* avail_virt = AllocateMemoryPages(1);
  void* used_virt = AllocateMemoryPages(1);
  if (!desc_virt || !avail_virt || !used_virt) return;

  memset(desc_virt, 0, 4096);
  memset(avail_virt, 0, 4096);
  memset(used_virt, 0, 4096);

  size = qsize;
  mem = desc_virt;
  phys = GetPhysicalAddressOfVirtualAddress((size_t)desc_virt);
  last_seen_used = 0;
  next_desc = 0;

  desc = (volatile VirtQueueDesc*)desc_virt;
  avail = (volatile VirtQueueAvail*)avail_virt;
  used = (volatile VirtQueueUsed*)used_virt;

  for (int i = 0; i < qsize; i++) {
    buffers_virt[i] = AllocateMemoryPages(1);
    buffers_phys[i] =
        GetPhysicalAddressOfVirtualAddress((size_t)buffers_virt[i]);
  }

  avail->flags = 1;
  avail->idx = 0;

  FlushRange(desc_virt, 4096);
  FlushRange(avail_virt, 4096);
  FlushRange(used_virt, 4096);

  uint64 desc_p = GetPhysicalAddressOfVirtualAddress((size_t)desc_virt);
  *(volatile uint64*)(&common_cfg[32]) = desc_p;

  uint64 avail_p = GetPhysicalAddressOfVirtualAddress((size_t)avail_virt);
  *(volatile uint64*)(&common_cfg[40]) = avail_p;

  uint64 used_p = GetPhysicalAddressOfVirtualAddress((size_t)used_virt);
  *(volatile uint64*)(&common_cfg[48]) = used_p;

  *(volatile uint16*)(&common_cfg[28]) = 1;  // Enable queue
  FlushRange((void*)common_cfg, 64);
}
