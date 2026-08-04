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

#include "virtio_pci_device.h"

#include "perception/cache.h"
#include "perception/interrupts.h"
#include "perception/memory.h"
#include "perception/pci.h"
#include "perception/port_io.h"
#include "virtio.h"

using ::perception::kPageSize;
using ::perception::MapPhysicalMemory;
using ::perception::Read16BitsFromPciConfig;
using ::perception::Read32BitsFromPciConfig;
using ::perception::Read8BitsFromPciConfig;
using ::perception::RegisterInterruptHandlerClearMmioByte;
using ::perception::RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort;
using ::perception::Write16BitsToPciConfig;
using ::perception::Write16BitsToPort;
using ::perception::devices::PciDevice;

namespace {

constexpr uint8 kPciCapMsiX = 0x11;
constexpr uint16 kPciCapMsixControlOffset = 2;
constexpr uint16 kPciCapMsixEnableBit = 1 << 15;

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

constexpr size_t kCommonCfgDeviceFeatureSelectOffset = 0;
constexpr size_t kCommonCfgDeviceFeatureOffset = 4;
constexpr size_t kCommonCfgDriverFeatureSelectOffset = 8;
constexpr size_t kCommonCfgDriverFeatureOffset = 12;
constexpr size_t kCommonCfgDeviceStatusOffset = 20;

constexpr size_t kPageMask = 4095;
constexpr uint8 kVirtioStatusReset = 0;

}  // namespace

VirtioPciDevice::VirtioPciDevice(const PciDevice& device) : device_(device) {}

bool VirtioPciDevice::Initialize() {
  EnableVirtioPciDevice(device_);
  interrupt_line_ = GetPciInterruptLine(device_);

  uint64 bar_phys[kMaxPciBars] = {};
  uint16 io_port_base = 0;

  for (int i = 0; i < kMaxPciBars; i++) {
    uint32 bar = Read32BitsFromPciConfig(
        device_.bus, device_.slot, device_.function, kPciConfigBar0Offset + i * 4);
    if (bar == 0 || bar == 0xFFFFFFFF) {
      continue;
    }
    if ((bar & 1) != 0) {  // I/O space BAR
      io_port_base = bar & kPciBarIoAddressMask;
    } else {
      uint64 phys = bar & kPciBarMemoryAddressMask;
      bool is_64bit = ((bar & kPciBar64BitBit) != 0);
      if (is_64bit && i + 1 < kMaxPciBars) {
        uint32 upper = Read32BitsFromPciConfig(
            device_.bus, device_.slot, device_.function,
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
      Read8BitsFromPciConfig(device_.bus, device_.slot, device_.function,
                             kPciConfigCapabilitiesPtrOffset);

  int max_caps = kMaxPciCapabilities;
  while (cap_ptr != 0 && cap_ptr != kInvalidCapPtr && max_caps-- > 0) {
    uint8 cap_id = Read8BitsFromPciConfig(device_.bus, device_.slot,
                                          device_.function, cap_ptr);
    uint8 next_cap = Read8BitsFromPciConfig(device_.bus, device_.slot,
                                            device_.function, cap_ptr + 1);

    if (cap_id == kPciCapMsiX) {
      uint16 msix_control = Read16BitsFromPciConfig(
          device_.bus, device_.slot, device_.function,
          cap_ptr + kPciCapMsixControlOffset);
      if (msix_control & kPciCapMsixEnableBit) {
        msix_control &= ~kPciCapMsixEnableBit;
        Write16BitsToPciConfig(device_.bus, device_.slot, device_.function,
                               cap_ptr + kPciCapMsixControlOffset,
                               msix_control);
      }
    } else if (cap_id == kPciCapVendorSpecific) {
      uint8 cfg_type =
          Read8BitsFromPciConfig(device_.bus, device_.slot, device_.function,
                                 cap_ptr + kVirtioPciCapTypeOffset);
      uint8 bar_idx =
          Read8BitsFromPciConfig(device_.bus, device_.slot, device_.function,
                                 cap_ptr + kVirtioPciCapBarOffset);
      uint32 offset =
          Read32BitsFromPciConfig(device_.bus, device_.slot, device_.function,
                                  cap_ptr + kVirtioPciCapOffsetOffset);
      uint32 length =
          Read32BitsFromPciConfig(device_.bus, device_.slot, device_.function,
                                  cap_ptr + kVirtioPciCapLengthOffset);

      if (bar_idx < kMaxPciBars && bar_phys[bar_idx] != 0 && length > 0) {
        uint64 cap_phys = bar_phys[bar_idx] + offset;
        size_t page_offset = cap_phys & kPageMask;
        size_t pages = (length + page_offset + kPageMask) / kPageSize;
        if (pages == 0) pages = 1;

        void* mapped = MapPhysicalMemory(cap_phys & ~kPageMask, pages);

        if (mapped != nullptr && (size_t)mapped != (size_t)-1) {
          volatile uint8* ptr = (volatile uint8*)mapped + page_offset;

          if (cfg_type == kVirtioPciCapCommonConfig) {
            common_cfg_ = ptr;
          } else if (cfg_type == kVirtioPciCapNotifyConfig) {
            notify_cfg_ = ptr;
            notify_off_multiplier_ = Read32BitsFromPciConfig(
                device_.bus, device_.slot, device_.function,
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
  return (common_cfg_ != nullptr || io_base_ != 0);
}

void VirtioPciDevice::Reset() {
  if (common_cfg_ != nullptr) {
    common_cfg_[kCommonCfgDeviceStatusOffset] = kVirtioStatusReset;
  } else if (io_base_ != 0) {
    ResetLegacyVirtioDevice(io_base_);
  }
}

void VirtioPciDevice::NegotiateFeatures(uint32 disable_features_mask) {
  if (common_cfg_ == nullptr) return;

  common_cfg_[kCommonCfgDeviceStatusOffset] = kVirtioStatusReset;
  common_cfg_[kCommonCfgDeviceStatusOffset] = kVirtioStatusAcknowledge;
  common_cfg_[kCommonCfgDeviceStatusOffset] =
      kVirtioStatusAcknowledge | kVirtioStatusDriver;

  *(volatile uint32*)(&common_cfg_[kCommonCfgDeviceFeatureSelectOffset]) = 0;
  uint32 dev_feat0 =
      *(volatile uint32*)(&common_cfg_[kCommonCfgDeviceFeatureOffset]);
  *(volatile uint32*)(&common_cfg_[kCommonCfgDeviceFeatureSelectOffset]) = 1;
  uint32 dev_feat1 =
      *(volatile uint32*)(&common_cfg_[kCommonCfgDeviceFeatureOffset]);

  dev_feat0 &= ~disable_features_mask;

  *(volatile uint32*)(&common_cfg_[kCommonCfgDriverFeatureSelectOffset]) = 0;
  *(volatile uint32*)(&common_cfg_[kCommonCfgDriverFeatureOffset]) = dev_feat0;

  *(volatile uint32*)(&common_cfg_[kCommonCfgDriverFeatureSelectOffset]) = 1;
  *(volatile uint32*)(&common_cfg_[kCommonCfgDriverFeatureOffset]) = dev_feat1;

  common_cfg_[kCommonCfgDeviceStatusOffset] = kVirtioStatusAcknowledge |
                                              kVirtioStatusDriver |
                                              kVirtioStatusFeaturesOk;
}

void VirtioPciDevice::SetDriverOk() {
  if (common_cfg_ != nullptr) {
    common_cfg_[kCommonCfgDeviceStatusOffset] =
        kVirtioStatusAcknowledge | kVirtioStatusDriver |
        kVirtioStatusFeaturesOk | kVirtioStatusDriverOk;
  } else if (io_base_ != 0) {
    SetVirtioDriverOk(io_base_);
  }
}

void VirtioPciDevice::KickQueue(const QueueDetails& queue) {
  if (common_cfg_ != nullptr && notify_cfg_ != nullptr) {
    uint16 notify_off = queue.notify_off;
    uint32 offset = notify_off * notify_off_multiplier_;
    *(volatile uint16*)(notify_cfg_ + offset) = 0;
  } else if (io_base_ != 0) {
    Write16BitsToPort(io_base_ + kVirtioPciQueueNotify, 0);
  }
}

void VirtioPciDevice::RegisterInterrupt(std::function<void()> handler,
                                         uint8 read_mask) {
  if (common_cfg_ != nullptr) {
    RegisterInterruptHandlerClearMmioByte(interrupt_line_, isr_phys_, handler);
  } else if (io_base_ != 0) {
    uint16 isr_port = io_base_ + kVirtioPciIsr;
    RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort(
        interrupt_line_, isr_port, read_mask, isr_port,
        [handler](const uint8*) { handler(); });
  }
}
