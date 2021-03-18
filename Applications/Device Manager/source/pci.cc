// Copyright 2020 Google LLC
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

#include "pci.h"

#include <iostream>
#include <functional>
#include <vector>

#include "pci_device_names.h"
#include "pci_drivers.h"
#include "perception/pci.h"

using ::perception::kPciHdrClassCode;
using ::perception::kPciHdrDeviceId;
using ::perception::kPciHdrHeaderType;
using ::perception::kPciHdrProgIf;
using ::perception::kPciHdrSecondaryBusNumber;
using ::perception::kPciHdrSubclass;
using ::perception::kPciHdrVendorId;
using ::perception::Read16BitsFromPciConfig;
using ::perception::Read8BitsFromPciConfig;

namespace {

struct PciDevice {
	uint8 base_class;
	uint8 sub_class;
	uint8 prog_if;
	uint16 vendor_id;
	uint16 device_id;
	uint8 bus;
	uint8 slot;
	uint8 function;
};

std::vector<PciDevice> devices;


void ForEachPciDeviceInBus(uint8 bus,
	const std::function<void(
		uint8, uint8, uint8, uint16, uint16, uint8, uint8, uint8)>& on_each_pci_device);

/* found a device */
void ParsePciBusSlotFunction(uint8 bus, uint8 slot, uint8 function,
	const std::function<void(
		uint8, uint8, uint8, uint16, uint16, uint8, uint8, uint8)>& on_each_pci_device) {

	uint8 base_class = Read8BitsFromPciConfig(bus, slot, function, kPciHdrClassCode);
	uint8 sub_class = Read8BitsFromPciConfig(bus, slot, function, kPciHdrSubclass);
	uint8 prog_if = Read8BitsFromPciConfig(bus, slot, function, kPciHdrProgIf);
	uint16 vendor_id = Read16BitsFromPciConfig(bus, slot, function, kPciHdrVendorId);
	uint16 device_id = Read16BitsFromPciConfig(bus, slot, function, kPciHdrDeviceId);

	if (base_class == 0x06 && sub_class == 0x04) {
		// PCI-to-PCI Bridge
		uint8 secondary_bus = Read8BitsFromPciConfig(
			bus, slot, function, kPciHdrSecondaryBusNumber);
		ForEachPciDeviceInBus(secondary_bus, on_each_pci_device);
	} else {
		on_each_pci_device(base_class, sub_class, prog_if, vendor_id, device_id,
			bus, slot, function);
	}
}

void ForEachPciDeviceInBusAndSlot(uint8 bus, uint8 slot,
	const std::function<void(
		uint8, uint8, uint8, uint16, uint16, uint8, uint8, uint8)>& on_each_pci_device) {
	/* check if there is a device here - on function 0 */
	uint16 vendor_id = Read16BitsFromPciConfig(bus, slot, 0, kPciHdrVendorId);
	if(vendor_id == 0xFFFF) return;

	/* check what functions it performs */
	ParsePciBusSlotFunction(bus, slot, 0, on_each_pci_device);
	uint16 header_type = Read8BitsFromPciConfig(bus, slot, 0, kPciHdrHeaderType);

	if((header_type & 0x80) != 0) { /* multi-function device */
		uint8 function;	for(function = 1; function < 8; function++) {
			if(Read16BitsFromPciConfig(bus, slot, function, kPciHdrVendorId) != 0xFFFF)
				ParsePciBusSlotFunction(bus, slot, function, on_each_pci_device);
		}
	}
}

void ForEachPciDeviceInBus(uint8 bus,
	const std::function<void(
		uint8, uint8, uint8, uint16, uint16, uint8, uint8, uint8)>& on_each_pci_device) {
	for(uint8 device = 0; device < 32; device++)
		ForEachPciDeviceInBusAndSlot(bus, device, on_each_pci_device);
}

void ForEachPciDevice(const std::function<void(
		uint8, uint8, uint8, uint16, uint16, uint8, uint8, uint8)>& on_each_pci_device) {
	/* scan buses for devices */
	uint16 header_type = Read16BitsFromPciConfig(0, 0, 0, kPciHdrVendorId);
	if((header_type & 0x80) == 0)
		/* single pci host controller */
		ForEachPciDeviceInBus(0, on_each_pci_device);
	else {
		/* multi pci host controllers */
		uint8 function; for(function = 0; function < 8; function++) {
			if(Read16BitsFromPciConfig(0, 0, function, kPciHdrVendorId) == 0xFFFF) /* reference said != but that's funny? */
				break;

			ForEachPciDeviceInBus(function, on_each_pci_device);
		}
	}
}

}

void InitializePci() {
	ForEachPciDevice([](
		uint8 base_class, uint8 sub_class, uint8 prog_if, uint16 vendor_id,
		uint16 device_id, uint8 bus, uint8 slot, uint8 function) {
		if (!LoadPciDriver(base_class, sub_class, prog_if, vendor_id,
			device_id, bus, slot, function)) {
			std::cout << "Encountered unknown PCI device at " <<
				(int)base_class << ":" << (int)sub_class << ":" <<
				(int)prog_if << ": " <<
				GetPciDeviceName(base_class, sub_class, prog_if) << std::endl;
		}

		PciDevice device;
		device.base_class = base_class;
		device.sub_class = sub_class;
		device.prog_if = prog_if;
		device.vendor_id = vendor_id;
		device.device_id = device_id;
		device.bus = bus;
		device.slot = slot;
		device.function = function;

		devices.push_back(device);
	});
}

void ForEachPciDeviceThatMatchesQuery(int16 base_class, int16 sub_class,
	int16 prog_if, int32 vendor_id, int32 device_id, int16 bus, int16 slot,
	int16 function, const std::function<void(uint8, uint8, uint8, uint16,
		uint16, uint8, uint8, uint8)>& on_each_device) {
	for (const PciDevice& device : devices) {
		if ((base_class == -1 || base_class == device.base_class) &&
			(sub_class == -1 || sub_class == device.sub_class) &&
			(prog_if == -1 || prog_if == device.prog_if) &&
			(vendor_id == -1 || vendor_id == device.vendor_id) &&
			(device_id == -1 || device_id == device.device_id) &&
			(bus == -1 || bus == device.bus) &&
			(slot == -1 || slot == device.slot) &&
			(function == -1 || function == device.function)) {
			on_each_device(device.base_class, device.sub_class,
				device.prog_if, device.vendor_id, device.device_id,
				device.bus, device.slot, device.function);
		}
	}

}
