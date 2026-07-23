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

#include "ahci.h"

#include <iostream>
#include <memory>
#include <vector>

#include "ahci_storage_device.h"
#include "ahci_types.h"
#include "perception/devices/device_manager.h"
#include "perception/memory.h"
#include "perception/pci.h"
#include "perception/services.h"

using ::perception::GetService;
using ::perception::kPciHdrBar5;
using ::perception::kPciHdrCommand;
using ::perception::kPciHdrCommandBitBusMaster;
using ::perception::kPciHdrCommandBitMemorySpace;
using ::perception::MapPhysicalMemory;
using ::perception::Read32BitsFromPciConfig;
using ::perception::Read8BitsFromPciConfig;
using ::perception::Write8BitsToPciConfig;
using ::perception::devices::DeviceManager;
using ::perception::devices::PciDeviceFilter;
using ::perception::devices::PciDeviceFilters;
using ::perception::devices::StorageDeviceType;

namespace {

std::vector<std::unique_ptr<AhciStorageDevice>> ahci_devices;

void InitializeAhciController(uint8 bus, uint8 slot, uint8 function) {
  // Turn on Bus Master and Memory Space in PCI config
  uint8 command = Read8BitsFromPciConfig(bus, slot, function, kPciHdrCommand);
  command |= (kPciHdrCommandBitBusMaster | kPciHdrCommandBitMemorySpace);
  Write8BitsToPciConfig(bus, slot, function, kPciHdrCommand, command);

  // Read BAR5 MMIO Address
  uint32 bar5 = Read32BitsFromPciConfig(bus, slot, function, kPciHdrBar5);
  size_t phys_base = bar5 & 0xFFFFFFF0;
  if (phys_base == 0) return;

  // Map 4 pages for AHCI registers (0x1000 bytes)
  void* mapped = MapPhysicalMemory(phys_base, 4);
  if (!mapped) return;

  HbaMem* hba = reinterpret_cast<HbaMem*>(mapped);

  // Enable AHCI Mode (GHC.AE = bit 31)
  hba->ghc |= (1U << 31);

  uint32 pi = hba->pi;
  for (int i = 0; i < 32; ++i) {
    if ((pi & (1U << i)) == 0) continue;

    HbaPort* port = &hba->ports[i];
    uint32 ssts = port->ssts;
    uint8 det = ssts & 0x0F;
    uint8 ipm = (ssts >> 8) & 0x0F;

    if (det == 3 && ipm == 1) {
      uint32 sig = port->sig;
      if (sig == kSataSigAta) {
        std::string drive_name =
            "SATA Disk " + std::to_string(ahci_devices.size() + 1);
        uint64 sector_count = 2000000;
        uint32 sector_size = 512;
        ahci_devices.push_back(std::make_unique<AhciStorageDevice>(
            port, i, sector_count, sector_size, drive_name,
            StorageDeviceType::HARD_DRIVE));
      } else if (sig == kSataSigAtapi) {
        std::string drive_name =
            "SATA Optical Drive " + std::to_string(ahci_devices.size() + 1);
        uint64 sector_count = 2000000;
        uint32 sector_size = 2048;
        ahci_devices.push_back(std::make_unique<AhciStorageDevice>(
            port, i, sector_count, sector_size, drive_name,
            StorageDeviceType::OPTICAL));
      }
    }
  }
}

}  // namespace

void InitializeAhciControllers() {
  PciDeviceFilters filters;

  PciDeviceFilter base_class_filter;
  base_class_filter.key = PciDeviceFilter::Key::BASE_CLASS;
  base_class_filter.value = 0x01;  // Storage
  filters.filters.push_back(base_class_filter);

  PciDeviceFilter sub_class_filter;
  sub_class_filter.key = PciDeviceFilter::Key::SUB_CLASS;
  sub_class_filter.value = 0x06;  // SATA
  filters.filters.push_back(sub_class_filter);

  auto status_or_devices = GetService<DeviceManager>().QueryPciDevices(filters);
  if (!status_or_devices) return;

  for (const auto& device : status_or_devices->devices)
    InitializeAhciController(device.bus, device.slot, device.function);
}
