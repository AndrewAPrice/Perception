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

#include "device_manager.h"

#include "pci.h"
#include "pci_device_names.h"

using ::perception::ProcessId;
using ::perception::devices::PciDevice;
using ::perception::devices::PciDeviceFilter;
using ::perception::devices::PciDeviceFilters;
using ::perception::devices::PciDevices;

namespace {
void ParseFiltersIntoParameters(const PciDeviceFilters& filters,
                                int16& base_class, int16& sub_class,
                                int16& prog_if, int16& vendor_id,
                                int32& device_id, int16& bus, int16& slot,
                                int16& function) {
  for (const auto& filter : filters.filters) {
    switch (filter.key) {
      case PciDeviceFilter::Key::BASE_CLASS:
        base_class = static_cast<int16>(filter.value);
        break;
      case PciDeviceFilter::Key::SUB_CLASS:
        sub_class = static_cast<int16>(filter.value);
        break;
      case PciDeviceFilter::Key::PROG_IF:
        prog_if = static_cast<int16>(filter.value);
        break;
      case PciDeviceFilter::Key::VENDOR:
        vendor_id = static_cast<int16>(filter.value);
        break;
      case PciDeviceFilter::Key::DEVICE_ID:
        device_id = static_cast<int32>(filter.value);
        break;
      case PciDeviceFilter::Key::BUS:
        bus = static_cast<int16>(filter.value);
        break;
      case PciDeviceFilter::Key::SLOT:
        slot = static_cast<int16>(filter.value);
        break;
      case PciDeviceFilter::Key::FUNCTION:
        function = static_cast<int16>(filter.value);
        break;
    }
  }
}
}  // namespace

StatusOr<PciDevices> DeviceManager::QueryPciDevices(
    const PciDeviceFilters& request) {
  int16 base_class = -1;
  int16 sub_class = -1;
  int16 prog_if = -1;
  int16 vendor_id = -1;
  int32 device_id = -1;
  int16 bus = -1;
  int16 slot = -1;
  int16 function = -1;
  ParseFiltersIntoParameters(request, base_class, sub_class, prog_if, vendor_id,
                             device_id, bus, slot, function);

  PciDevices devices;
  ForEachPciDeviceThatMatchesQuery(
      base_class, sub_class, prog_if, vendor_id, device_id, bus, slot, function,
      [&](uint8 base_class, uint8 sub_class, uint8 prog_if, uint16 vendor,
          uint16 device_id, uint8 bus, uint8 slot, uint8 function) {
        devices.devices.push_back({});
        auto& device = devices.devices.back();
        device.base_class = base_class;
        device.sub_class = sub_class;
        device.prog_if = prog_if;
        device.vendor = vendor;
        device.device_id = device_id;
        device.bus = bus;
        device.slot = slot;
        device.function = function;
        device.name = GetPciDeviceName(base_class, sub_class, prog_if);
      });

  return devices;
}