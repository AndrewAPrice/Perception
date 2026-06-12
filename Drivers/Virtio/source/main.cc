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

#include <iostream>
#include <memory>
#include <vector>

#include "driver.h"
#include "perception/devices/device_manager.h"
#include "perception/pci.h"
#include "perception/services.h"
#include "virtio_network_device.h"

using ::perception::GetService;
using ::perception::Read16BitsFromPciConfig;
using ::perception::devices::DeviceManager;
using ::perception::devices::PciDeviceFilter;
using ::perception::devices::PciDeviceFilters;

int main() {
  PciDeviceFilters filters;
  PciDeviceFilter vendor_filter;
  vendor_filter.key = PciDeviceFilter::Key::VENDOR;
  vendor_filter.value = 0x1AF4;  // Virtio Vendor ID
  filters.filters.push_back(vendor_filter);

  auto status_or_devices = GetService<DeviceManager>().QueryPciDevices(filters);
  if (!status_or_devices) {
    std::cout << "Failed to query PCI devices." << std::endl;
    return 0;
  }

  std::vector<std::shared_ptr<Driver>> driver_instances;

  for (const auto& device : status_or_devices->devices) {
    uint16 device_id =
        Read16BitsFromPciConfig(device.bus, device.slot, device.function, 2);
    switch (device_id) {
      case 0x1000:  // Legacy virtio-net.
      case 0x1041:  // Modern virtio-net.
        std::cout << "Found Virtio Network Card device_id=0x" << std::hex
                  << device_id << std::dec << std::endl;
        driver_instances.push_back(
            std::make_shared<VirtioNetworkDevice>(device));
        break;
      default:
        std::cout << "Unhandled Virtio device_id=0x" << std::hex << device_id
                  << std::dec << std::endl;
        break;
    }
  }

  if (driver_instances.empty()) {
    std::cout << "No Virtio devices found." << std::endl;
    return 0;
  }

  perception::HandOverControl();
  return 0;
}
