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

#include "virtio_tablet_device.h"

#include <algorithm>
#include <iostream>

#include "perception/cache.h"
#include "perception/interrupts.h"
#include "perception/memory.h"
#include "perception/pci.h"
#include "perception/port_io.h"
#include "queue.h"
#include "status.h"
#include "types.h"
#include "virtio.h"

using ::perception::FlushRange;
using ::perception::kPageSize;
using ::perception::MapPhysicalMemory;
using perception::Read16BitsFromPciConfig;
using ::perception::Read32BitsFromPciConfig;
using ::perception::Read8BitsFromPciConfig;
using ::perception::RegisterInterruptHandlerClearMmioByte;
using ::perception::RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort;
using ::perception::Write16BitsToPciConfig;
using ::perception::Write16BitsToPort;
using ::perception::devices::MouseButton;
using ::perception::devices::MouseButtonEvent;
using ::perception::devices::MouseCaptureState;
using ::perception::devices::PciDevice;
using ::perception::devices::TabletHoverEvent;
using ::perception::devices::TabletListener;

namespace {

constexpr uint8 kPciCapMsiX = 0x11;
constexpr uint32 kPciBarIoAddressMask = 0xFFFC;
constexpr uint32 kPciBarMemoryAddressMask = 0xFFFFFFF0;
constexpr uint32 kPciBar64BitBit = 4;
constexpr uint8 kPciConfigBar0Offset = 0x10;
constexpr uint8 kPciConfigCapabilitiesPtrOffset = 0x34;
constexpr int kMaxPciBars = 6;
constexpr int kMaxPciCapabilities = 48;
constexpr uint8 kPciCapVendorSpecific = 0x09;
constexpr uint8 kInvalidCapPtr = 0xFF;

constexpr size_t kVirtioPciCapTypeOffset = 3;
constexpr size_t kVirtioPciCapBarOffset = 4;
constexpr size_t kVirtioPciCapOffsetOffset = 8;
constexpr size_t kVirtioPciCapLengthOffset = 12;
constexpr size_t kVirtioPciCapNotifyOffMultiplierOffset = 16;

constexpr uint8 kVirtioPciCapCommonConfig = 1;
constexpr uint8 kVirtioPciCapNotifyConfig = 2;
constexpr uint8 kVirtioPciCapIsrConfig = 3;
constexpr uint8 kVirtioPciCapDeviceConfig = 4;

constexpr size_t kCommonCfgDriverFeatureSelectOffset = 8;
constexpr size_t kCommonCfgDriverFeatureOffset = 12;
constexpr size_t kCommonCfgDeviceStatusOffset = 20;

constexpr size_t kPageMask = 4095;
constexpr uint8 kVirtioStatusReset = 0;

// VirtIO Input Event Constants
constexpr uint16 kEvSyn = 0x00;
constexpr uint16 kEvKey = 0x01;
constexpr uint16 kEvRel = 0x02;
constexpr uint16 kEvAbs = 0x03;

constexpr uint16 kSynReport = 0x00;

constexpr uint16 kAbsX = 0x00;
constexpr uint16 kAbsY = 0x01;

constexpr uint16 kBtnLeft = 0x110;    // 272
constexpr uint16 kBtnRight = 0x111;   // 273
constexpr uint16 kBtnMiddle = 0x112;  // 274

constexpr float kVirtioAbsMax = 32767.0f;
constexpr uint16 kVringDescFWrite = 2;
constexpr uint8 kIsrReadMask = 1;

}  // namespace

VirtioTabletDevice::VirtioTabletDevice(const PciDevice& device)
    : TabletDevice::Server({.defer_registration = true}), device_(device) {
  EnableVirtioPciDevice(device);

  uint8 interrupt_line = GetPciInterruptLine(device);

  uint64 bar_phys[kMaxPciBars] = {};
  uint16 io_port_base = 0;

  for (int i = 0; i < kMaxPciBars; i++) {
    uint32 bar = Read32BitsFromPciConfig(
        device.bus, device.slot, device.function, kPciConfigBar0Offset + i * 4);
    if (bar == 0 || bar == 0xFFFFFFFF) {
      continue;
    }
    if ((bar & 1) != 0) {  // I/O space BAR
      io_port_base = bar & kPciBarIoAddressMask;
    } else {
      uint64 phys = bar & kPciBarMemoryAddressMask;
      bool is_64bit = ((bar & kPciBar64BitBit) != 0);
      if (is_64bit && i + 1 < kMaxPciBars) {
        uint32 upper =
            Read32BitsFromPciConfig(device.bus, device.slot, device.function,
                                    kPciConfigBar0Offset + (i + 1) * 4);
        phys |= ((uint64)upper << 32);
        bar_phys[i] = phys;
        bar_phys[i + 1] = phys;
        i++;
      } else {
        bar_phys[i] = phys;
      }
    }
  }

  uint8 cap_ptr =
      Read8BitsFromPciConfig(device.bus, device.slot, device.function,
                             kPciConfigCapabilitiesPtrOffset);

  int max_caps = kMaxPciCapabilities;
  while (cap_ptr != 0 && cap_ptr != kInvalidCapPtr && max_caps-- > 0) {
    uint8 cap_id = Read8BitsFromPciConfig(device.bus, device.slot,
                                          device.function, cap_ptr);
    uint8 next_cap = Read8BitsFromPciConfig(device.bus, device.slot,
                                            device.function, cap_ptr + 1);

    if (cap_id == kPciCapMsiX) {
      uint16 msix_control = Read16BitsFromPciConfig(
          device.bus, device.slot, device.function, cap_ptr + 2);
      if (msix_control & (1 << 15)) {
        msix_control &= ~(1 << 15);
        Write16BitsToPciConfig(device.bus, device.slot, device.function,
                               cap_ptr + 2, msix_control);
      }
    } else if (cap_id == kPciCapVendorSpecific) {
      uint8 cfg_type =
          Read8BitsFromPciConfig(device.bus, device.slot, device.function,
                                 cap_ptr + kVirtioPciCapTypeOffset);
      uint8 bar_idx =
          Read8BitsFromPciConfig(device.bus, device.slot, device.function,
                                 cap_ptr + kVirtioPciCapBarOffset);
      uint32 offset =
          Read32BitsFromPciConfig(device.bus, device.slot, device.function,
                                  cap_ptr + kVirtioPciCapOffsetOffset);
      uint32 length =
          Read32BitsFromPciConfig(device.bus, device.slot, device.function,
                                  cap_ptr + kVirtioPciCapLengthOffset);

      if (bar_idx >= kMaxPciBars) {
        std::cout << "Invalid BAR index " << (int)bar_idx << std::endl;
      } else if (bar_phys[bar_idx] == 0) {
        std::cout << "BAR " << (int)bar_idx << " physical address is 0!"
                  << std::endl;
      } else if (length == 0) {
        std::cout << "Capability length is 0!" << std::endl;
      } else {
        uint64 cap_phys = bar_phys[bar_idx] + offset;
        size_t page_offset = cap_phys & kPageMask;
        size_t pages = (length + page_offset + kPageMask) / kPageSize;
        if (pages == 0) pages = 1;

        void* mapped = MapPhysicalMemory(cap_phys & ~kPageMask, pages);

        if (mapped == nullptr || (size_t)mapped == (size_t)-1) {
          std::cout << "MapPhysicalMemory failed for phys 0x" << std::hex
                    << cap_phys << std::dec << std::endl;
        } else {
          volatile uint8* ptr = (volatile uint8*)mapped + page_offset;

          if (cfg_type == kVirtioPciCapCommonConfig) {
            common_cfg_ = ptr;
          } else if (cfg_type == kVirtioPciCapNotifyConfig) {
            notify_cfg_ = ptr;
            notify_off_multiplier_ = Read32BitsFromPciConfig(
                device.bus, device.slot, device.function,
                cap_ptr + kVirtioPciCapNotifyOffMultiplierOffset);
          } else if (cfg_type == kVirtioPciCapIsrConfig) {
            isr_cfg_ = ptr;
            isr_phys_ = cap_phys;
          } else if (cfg_type == kVirtioPciCapDeviceConfig) {
            device_cfg_ = ptr;
          }
        }
      }
    }
    cap_ptr = next_cap;
  }

  io_base_ = io_port_base;

  if (common_cfg_ != nullptr) {
    common_cfg_[kCommonCfgDeviceStatusOffset] = kVirtioStatusReset;
    common_cfg_[kCommonCfgDeviceStatusOffset] = kVirtioStatusAcknowledge;
    common_cfg_[kCommonCfgDeviceStatusOffset] =
        kVirtioStatusAcknowledge | kVirtioStatusDriver;

    // Read host offered features.
    *(volatile uint32*)(&common_cfg_[0]) = 0;
    uint32 dev_feat0 = *(volatile uint32*)(&common_cfg_[4]);
    *(volatile uint32*)(&common_cfg_[0]) = 1;
    uint32 dev_feat1 = *(volatile uint32*)(&common_cfg_[4]);

    // Mask off VIRTIO_F_EVENT_IDX (bit 29) to prevent interrupt suppression.
    dev_feat0 &= ~(1U << 29);

    // Write accepted driver features
    *(volatile uint32*)(&common_cfg_[kCommonCfgDriverFeatureSelectOffset]) = 0;
    *(volatile uint32*)(&common_cfg_[kCommonCfgDriverFeatureOffset]) =
        dev_feat0;

    *(volatile uint32*)(&common_cfg_[kCommonCfgDriverFeatureSelectOffset]) = 1;
    *(volatile uint32*)(&common_cfg_[kCommonCfgDriverFeatureOffset]) =
        dev_feat1;

    common_cfg_[kCommonCfgDeviceStatusOffset] = kVirtioStatusAcknowledge |
                                                kVirtioStatusDriver |
                                                kVirtioStatusFeaturesOk;
    uint8 st_feat = common_cfg_[kCommonCfgDeviceStatusOffset];

    if ((st_feat & kVirtioStatusFeaturesOk) == 0) {
      std::cout
          << "Device rejected negotiated features! (FEATURES_OK bit missing)"
          << std::endl;
    }

    // Setup Queue 0 (eventq) and Queue 1 (statusq)
    *(volatile uint16*)(&common_cfg_[16]) = 0xFFFF;  // msix_config = NO_VECTOR
    event_queue_.SetupModern(0, common_cfg_);
    *(volatile uint16*)(&common_cfg_[26]) =
        0xFFFF;  // queue_msix_vector = NO_VECTOR

    status_queue_.SetupModern(1, common_cfg_);
    *(volatile uint16*)(&common_cfg_[26]) =
        0xFFFF;  // queue_msix_vector = NO_VECTOR

    // Populate event queue buffers
    for (size_t i = 0; i < event_queue_.size; i++) {
      event_queue_.desc[i].addr = event_queue_.buffers_phys[i];
      event_queue_.desc[i].len = sizeof(VirtioInputEvent);
      event_queue_.desc[i].flags = kVringDescFWrite;
      event_queue_.desc[i].next = 0;
      event_queue_.avail->ring[i] = static_cast<uint16>(i);
    }
    event_queue_.avail->idx = event_queue_.size;

    FlushRange((void*)event_queue_.desc, kPageSize);
    FlushRange((void*)event_queue_.avail, kPageSize);
    FlushRange((void*)event_queue_.used, kPageSize);
  } else if (io_base_ != 0) {
    ResetLegacyVirtioDevice(io_base_);

    event_queue_.Setup(0, io_base_);
    status_queue_.Setup(1, io_base_);

    if (event_queue_.desc && event_queue_.avail) {
      for (size_t i = 0; i < event_queue_.size; i++) {
        event_queue_.desc[i].addr = event_queue_.buffers_phys[i];
        event_queue_.desc[i].len = sizeof(VirtioInputEvent);
        event_queue_.desc[i].flags = kVringDescFWrite;
        event_queue_.desc[i].next = 0;
        event_queue_.avail->ring[i] = static_cast<uint16>(i);
      }
      event_queue_.avail->idx = event_queue_.size;
    } else {
      std::cout << "Legacy event queue pointers invalid!" << std::endl;
    }
  } else {
    std::cout << "Neither common_cfg_ nor io_base_ available!" << std::endl;
    return;
  }

  if (!event_queue_.avail || event_queue_.size == 0) {
    std::cout << "Virtqueue setup failed! (event_queue_.avail="
              << (void*)event_queue_.avail << ", size=" << event_queue_.size
              << ")" << std::endl;
    return;
  }

  if (common_cfg_ != nullptr) {
    RegisterInterruptHandlerClearMmioByte(interrupt_line, isr_phys_,
                                          [this]() { HandleInterrupt(); });
  } else if (io_base_ != 0) {
    uint16 isr_port = io_base_ + kVirtioPciIsr;
    RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort(
        interrupt_line, isr_port, kIsrReadMask, isr_port,
        [this](const uint8*) { HandleInterrupt(); });
  }

  // Set DRIVER_OK and notify queue AFTER interrupt handler is registered.
  if (common_cfg_ != nullptr) {
    common_cfg_[kCommonCfgDeviceStatusOffset] =
        kVirtioStatusAcknowledge | kVirtioStatusDriver |
        kVirtioStatusFeaturesOk | kVirtioStatusDriverOk;

    if (notify_cfg_) {
      uint16 notify_off = event_queue_.notify_off;
      uint32 offset = notify_off * notify_off_multiplier_;
      *(volatile uint16*)(notify_cfg_ + offset) = 0;
    }
  } else if (io_base_ != 0) {
    SetVirtioDriverOk(io_base_);
    Write16BitsToPort(io_base_ + kVirtioPciQueueNotify, 0);
  }

  StartServing();
}

Status VirtioTabletDevice::SetTabletListener(
    const TabletListener::Client& listener) {
  if (listener.IsValid()) {
    tablet_listener_ = std::make_unique<TabletListener::Client>(listener);
  } else {
    tablet_listener_.reset();
  }

  // Force doorbell resynchronization.
  if (common_cfg_ != nullptr && notify_cfg_) {
    uint16 notify_off = event_queue_.notify_off;
    uint32 offset = notify_off * notify_off_multiplier_;
    *(volatile uint16*)(notify_cfg_ + offset) = 0;
  }
  return Status::OK;
}

Status VirtioTabletDevice::SetMouseCaptured(const MouseCaptureState& state) {
  is_captured_ = state.is_captured;
  return Status::OK;
}

void VirtioTabletDevice::HandleInterrupt() {
  if (isr_cfg_ != nullptr) {
    (void)*isr_cfg_;
  }

  uint16 newly_added_buffers = 0;

  while (event_queue_.last_seen_used != event_queue_.used->idx) {
    uint16 used_idx = event_queue_.last_seen_used % event_queue_.size;
    uint32 desc_id = event_queue_.used->ring[used_idx].id;
    event_queue_.last_seen_used++;

    if (desc_id < event_queue_.size) {
      VirtioInputEvent ev =
          *(VirtioInputEvent*)event_queue_.buffers_virt[desc_id];

      switch (ev.type) {
        case kEvAbs:
          if (ev.code == kAbsX) {
            current_x_ =
                std::max(0.0f, std::min(1.0f, ev.value / kVirtioAbsMax));
            position_changed_ = true;
          } else if (ev.code == kAbsY) {
            current_y_ =
                std::max(0.0f, std::min(1.0f, ev.value / kVirtioAbsMax));
            position_changed_ = true;
          }
          break;
        case kEvKey: {
          MouseButton button = MouseButton::Unknown;
          if (ev.code == kBtnLeft) {
            button = MouseButton::Left;
          } else if (ev.code == kBtnRight) {
            button = MouseButton::Right;
          } else if (ev.code == kBtnMiddle) {
            button = MouseButton::Middle;
          }

          if (button != MouseButton::Unknown && tablet_listener_) {
            MouseButtonEvent btn_event;
            btn_event.button = button;
            btn_event.is_pressed_down = (ev.value != 0);
            tablet_listener_->TabletButton(btn_event, nullptr);
          }
          break;
        }
        case kEvSyn:
          if (ev.code == kSynReport) {
            if (position_changed_) {
              if (tablet_listener_) {
                TabletHoverEvent pos_event;
                pos_event.x = current_x_;
                pos_event.y = current_y_;
                tablet_listener_->TabletHover(pos_event, nullptr);
              }
              position_changed_ = false;
            }
          }
          break;
      }

      // Re-add descriptor back to available ring
      uint16 avail_slot = event_queue_.avail->idx % event_queue_.size;
      event_queue_.avail->ring[avail_slot] = static_cast<uint16>(desc_id);
      event_queue_.avail->idx = event_queue_.avail->idx + 1;
      newly_added_buffers++;
    }
  }

  if (newly_added_buffers > 0) {
    FlushRange((void*)event_queue_.avail, kPageSize);
    if (common_cfg_ != nullptr && notify_cfg_) {
      uint16 notify_off = event_queue_.notify_off;
      uint32 offset = notify_off * notify_off_multiplier_;
      *(volatile uint16*)(notify_cfg_ + offset) = 0;
    } else if (io_base_ != 0) {
      Write16BitsToPort(io_base_ + kVirtioPciQueueNotify, 0);
    }
  }
}
