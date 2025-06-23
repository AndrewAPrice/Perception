// Copyright 2025 Google LLC
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
// #define PERCEPTION
#include "perception/devices/device_manager.h"

#include "perception/serialization/serializer.h"

namespace perception {
namespace devices {

void PciDevice::Serialize(serialization::Serializer& serializer) {
  serializer.String("Name", name);
  serializer.Integer("Base class", base_class);
  serializer.Integer("Sub class", sub_class);
  serializer.Integer("Prog if", prog_if);
  serializer.Integer("Vendor", vendor);
  serializer.Integer("Device ID", device_id),
  serializer.Integer("Bus", bus);
  serializer.Integer("Slot", slot);
  serializer.Integer("Function", function);
}

void PciDevices::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Devices", devices);
}

void PciDeviceFilter::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Key", key);
  serializer.Integer("Value", value);
}

void PciDeviceFilters::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Filters", filters);;
}

}  // namespace devices
}  // namespace perception
