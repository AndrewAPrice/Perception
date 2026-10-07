// Copyright 2021 Google LLC
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

#include "output/framebuffer.h"

#ifndef TEST
#include "../../../third_party/multiboot2.h"
#include "hardware/io.h"
#include "loader/multiboot_modules.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#include "output/text_terminal.h"
#endif

namespace output {

namespace {

#ifndef TEST
// PCI configuration address port.
constexpr uint16 kPciConfigAddressPort = 0xCF8;

// PCI configuration data port.
constexpr uint16 kPciConfigDataPort = 0xCFC;

// Virtio PCI vendor ID (Red Hat / Qumranet).
constexpr uint16 kVirtioPciVendorId = 0x1AF4;

// Virtio GPU PCI device ID.
constexpr uint16 kVirtioGpuDeviceId = 0x1050;

// PCI BAR0 configuration offset.
constexpr uint8 kPciBar0Offset = 0x10;

// PCI capabilities pointer offset.
constexpr uint8 kPciCapabilitiesPtrOffset = 0x34;

// PCI vendor-specific capability ID.
constexpr uint8 kPciCapVendorSpecific = 0x09;

// Virtio PCI common configuration capability type.
constexpr uint8 kVirtioPciCapCommonConfig = 1;

// Offset of the device_status byte inside Virtio PCI common configuration.
constexpr size_t kVirtioCommonCfgDeviceStatusOffset = 20;

// Offset of the device_status byte in Legacy Virtio I/O space.
constexpr uint16 kVirtioLegacyDeviceStatusOffset = 18;

// Bochs VBE Display Interface index port.
constexpr uint16 kVbeDispiIndexPort = 0x01CE;

// Bochs VBE Display Interface data port.
constexpr uint16 kVbeDispiDataPort = 0x01CF;

// Bochs VBE Display Interface register indices.
constexpr uint16 kVbeDispiIndexXres = 1;
constexpr uint16 kVbeDispiIndexYres = 2;
constexpr uint16 kVbeDispiIndexBpp = 3;
constexpr uint16 kVbeDispiIndexEnable = 4;

// Bochs VBE Display Interface enable flags (ENABLED | LFB_ENABLED | NOCLEARMEM).
constexpr uint16 kVbeDispiEnableFlags = 0x00E1;
#endif

// Frame buffer details saved from the multiboot header.
size_t g_framebuffer_address = 0;
uint8* g_framebuffer_virtual_address = nullptr;
uint32 g_framebuffer_width = 0;
uint32 g_framebuffer_height = 0;
uint32 g_framebuffer_pitch = 0;
uint8 g_framebuffer_bits_per_pixel = 0;
volatile uint8* g_virtio_gpu_status_register = nullptr;
uint16 g_virtio_gpu_io_base = 0;

#ifndef TEST
// Reads a 32-bit word from PCI configuration space on bus 0.
uint32 ReadPciConfig32(uint8 slot, uint8 func, uint8 offset) {
  uint32 address = (1U << 31) | (static_cast<uint32>(slot) << 11) |
                   (static_cast<uint32>(func) << 8) | (offset & 0xFC);
  hardware::WriteIO32Bits(kPciConfigAddressPort, address);
  return hardware::ReadIO32Bits(kPciConfigDataPort);
}

// Reads a single byte from PCI configuration space on bus 0.
uint8 ReadPciConfig8(uint8 slot, uint8 func, uint8 offset) {
  uint32 word = ReadPciConfig32(slot, func, offset);
  return static_cast<uint8>((word >> ((offset & 3) * 8)) & 0xFF);
}

// Scans PCI bus 0 for a Virtio GPU device and maps its status register so the
// kernel can reset it back to VBE framebuffer scanout mode during a crash.
void MaybeMapVirtioGpuStatusRegister() {
  for (uint8 slot = 0; slot < 32; slot++) {
    uint32 vendor_device = ReadPciConfig32(slot, 0, 0);
    uint16 vendor = static_cast<uint16>(vendor_device & 0xFFFF);
    uint16 device = static_cast<uint16>((vendor_device >> 16) & 0xFFFF);
    if (vendor != kVirtioPciVendorId || device != kVirtioGpuDeviceId) continue;

    uint64 bar_phys[6] = {0};
    for (int i = 0; i < 6; i++) {
      uint32 bar = ReadPciConfig32(slot, 0, kPciBar0Offset + i * 4);
      if (bar == 0 || bar == 0xFFFFFFFF) continue;
      if ((bar & 1) != 0) {
        g_virtio_gpu_io_base = static_cast<uint16>(bar & 0xFFFC);
      } else {
        uint64 phys = bar & 0xFFFFFFF0ULL;
        if ((bar & 4) != 0 && i + 1 < 6) {
          uint32 upper = ReadPciConfig32(slot, 0, kPciBar0Offset + (i + 1) * 4);
          phys |= (static_cast<uint64>(upper) << 32);
          bar_phys[i] = phys;
          bar_phys[i + 1] = phys;
          i++;
        } else {
          bar_phys[i] = phys;
        }
      }
    }

    uint8 cap_ptr = ReadPciConfig8(slot, 0, kPciCapabilitiesPtrOffset);
    for (int iter = 0; iter < 48 && cap_ptr != 0 && cap_ptr != 0xFF; iter++) {
      uint8 cap_id = ReadPciConfig8(slot, 0, cap_ptr);
      uint8 next_cap = ReadPciConfig8(slot, 0, cap_ptr + 1);
      if (cap_id == kPciCapVendorSpecific) {
        uint8 cfg_type = ReadPciConfig8(slot, 0, cap_ptr + 3);
        uint8 bar_idx = ReadPciConfig8(slot, 0, cap_ptr + 4);
        uint32 offset = ReadPciConfig32(slot, 0, cap_ptr + 8);
        if (cfg_type == kVirtioPciCapCommonConfig && bar_idx < 6 &&
            bar_phys[bar_idx] != 0) {
          size_t status_phys = bar_phys[bar_idx] + offset +
                               kVirtioCommonCfgDeviceStatusOffset;
          size_t page_offset = status_phys & (memory::kPageSize - 1);
          size_t mapped_page =
              memory::KernelAddressSpace().MapPhysicalPages(status_phys, 1);
          if (mapped_page != kOutOfMemory) {
            g_virtio_gpu_status_register =
                reinterpret_cast<volatile uint8*>(mapped_page + page_offset);
          }
          return;
        }
      }
      cap_ptr = next_cap;
    }
    return;
  }
}

// Initializes the framebuffer details and maps the buffer into kernel space.
void SetFramebufferDetails(size_t address, uint32 width, uint32 height,
                           uint32 pitch, uint8 bpp) {
  g_framebuffer_address = address;
  g_framebuffer_width = width;
  g_framebuffer_height = height;
  g_framebuffer_pitch = pitch;
  g_framebuffer_bits_per_pixel = bpp;

  size_t offset = address & (memory::kPageSize - 1);
  size_t size_bytes = static_cast<size_t>(height) * static_cast<size_t>(pitch);
  size_t pages =
      (offset + size_bytes + memory::kPageSize - 1) / memory::kPageSize;
  size_t mapped_base =
      memory::KernelAddressSpace().MapPhysicalPages(address, pages);
  if (mapped_base == kOutOfMemory) {
    g_framebuffer_virtual_address = nullptr;
  } else {
    g_framebuffer_virtual_address =
        reinterpret_cast<uint8*>(mapped_base + offset);
  }
  MaybeMapVirtioGpuStatusRegister();
}
#endif

}  // namespace

#ifndef TEST
void MaybeLoadFramebuffer() {
  // Initialize to empty values, in case a framebuffer isn't found in the
  // multiboot header.
  g_framebuffer_address = 0;
  g_framebuffer_virtual_address = nullptr;
  g_framebuffer_width = 0;
  g_framebuffer_height = 0;
  g_framebuffer_pitch = 0;
  g_framebuffer_bits_per_pixel = 0;

  size_t multiboot_phys = 0;
  size_t multiboot_address = 0;
  size_t multiboot_total_size = 0;
  if (!loader::GetMappedMultibootHeader(multiboot_phys, multiboot_address,
                                        multiboot_total_size))
    return;

  size_t multiboot_end = multiboot_address + multiboot_total_size;

  // Loop through the multiboot sections.
  for (multiboot_tag* tag = (multiboot_tag*)(multiboot_address + 8);
       tag != nullptr && (size_t)tag + sizeof(multiboot_tag) <= multiboot_end &&
       tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (tag->size < 8)
                 ? nullptr
                 : (multiboot_tag*)((size_t)tag +
                                    (size_t)((tag->size + 7) & ~7))) {
    // Found a framebuffer.
    if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
      multiboot_tag_framebuffer* tagfb = (multiboot_tag_framebuffer*)tag;
      if (tagfb->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        SetFramebufferDetails(
            tagfb->common.framebuffer_addr, tagfb->common.framebuffer_width,
            tagfb->common.framebuffer_height, tagfb->common.framebuffer_pitch,
            tagfb->common.framebuffer_bpp);
      } else {
        print << "Found a VESA framebuffer tag, but the framebuffer "
                 "is not of type MULTIBOOT_FRAMEBUFFER_TYPE_RGB.\n";
      }
    }
  }
}

void PopulateRegistersWithFramebufferDetails(hardware::Registers& regs) {
  regs.rax = g_framebuffer_address;
  regs.rbx = (size_t)g_framebuffer_width;
  regs.rdx = (size_t)g_framebuffer_height;
  regs.rsi = (size_t)g_framebuffer_pitch;
  regs.r8 = (size_t)g_framebuffer_bits_per_pixel;
}
#endif

void ReclaimFramebufferForBlueScreen() {
#ifndef TEST
  static bool reclaimed_once = false;
  bool virtio_was_active = false;
  if (g_virtio_gpu_status_register != nullptr &&
      *g_virtio_gpu_status_register != 0) {
    *g_virtio_gpu_status_register = 0;
    virtio_was_active = true;
  }
  if (!reclaimed_once || virtio_was_active) {
    reclaimed_once = true;
    if (g_virtio_gpu_io_base != 0)
      hardware::WriteIOByte(
          g_virtio_gpu_io_base + kVirtioLegacyDeviceStatusOffset, 0);
    if (g_framebuffer_width > 0 && g_framebuffer_height > 0 &&
        g_framebuffer_bits_per_pixel > 0) {
      hardware::WriteIO16Bits(kVbeDispiIndexPort, kVbeDispiIndexXres);
      hardware::WriteIO16Bits(kVbeDispiDataPort,
                              static_cast<uint16>(g_framebuffer_width));
      hardware::WriteIO16Bits(kVbeDispiIndexPort, kVbeDispiIndexYres);
      hardware::WriteIO16Bits(kVbeDispiDataPort,
                              static_cast<uint16>(g_framebuffer_height));
      hardware::WriteIO16Bits(kVbeDispiIndexPort, kVbeDispiIndexBpp);
      hardware::WriteIO16Bits(
          kVbeDispiDataPort, static_cast<uint16>(g_framebuffer_bits_per_pixel));
      hardware::WriteIO16Bits(kVbeDispiIndexPort, kVbeDispiIndexEnable);
      hardware::WriteIO16Bits(kVbeDispiDataPort, kVbeDispiEnableFlags);
    }
  }
#endif
}

FramebufferDetails GetFramebufferDetails() {
  return FramebufferDetails{
      .buffer = g_framebuffer_virtual_address,
      .width = g_framebuffer_width,
      .height = g_framebuffer_height,
      .pitch = g_framebuffer_pitch,
      .bits_per_pixel = g_framebuffer_bits_per_pixel,
  };
}

void SetFramebufferDetailsForTest(uint8* buffer, uint32 width, uint32 height,
                                  uint32 pitch, uint8 bpp) {
  g_framebuffer_virtual_address = buffer;
  g_framebuffer_width = width;
  g_framebuffer_height = height;
  g_framebuffer_pitch = pitch;
  g_framebuffer_bits_per_pixel = bpp;
}

}  // namespace output
