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
using ::permebuf::perception::devices::PciDevice;
using DM = ::permebuf::perception::devices::DeviceManager;

StatusOr<Permebuf<DM::QueryPciDevicesResponse>>
	DeviceManager::HandleQueryPciDevices(
	ProcessId sender,
	const DM::QueryPciDevicesRequest& request) {

	Permebuf<DM::QueryPciDevicesResponse> response;
	PermebufListOf<PciDevice> last_device;

	ForEachPciDeviceThatMatchesQuery(request.GetBaseClass(),
		request.GetSubClass(), request.GetProgIf(), request.GetVendor(),
		request.GetDeviceId(), request.GetBus(), request.GetSlot(),
		request.GetFunction(),
		[&] (uint8 base_class, uint8 sub_class, uint8 prog_if, uint16 vendor,
			uint16 device_id, uint8 bus, uint8 slot, uint8 function) {
			if (last_device.IsValid()) {
				last_device = last_device.InsertAfter();
			} else {
				last_device = response->MutableDevices();
			}
			auto device = response.AllocateMessage<PciDevice>();
			device.SetBaseClass(base_class);
			device.SetSubClass(sub_class);
			device.SetProgIf(prog_if);
			device.SetVendor(vendor);
			device.SetDeviceId(device_id);
			device.SetBus(bus);
			device.SetSlot(slot);
			device.SetFunction(function);
			device.SetName(GetPciDeviceName(base_class, sub_class, prog_if));
			last_device.Set(device);
		});

	return response;
}