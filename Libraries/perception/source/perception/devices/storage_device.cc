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
#include "perception/devices/storage_device.h"

#include "perception/serialization/serializer.h"

namespace perception {
namespace devices {

void StorageDeviceDetails::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Size in bytes", size_in_bytes);
  serializer.Integer("Is writable", is_writable);
  serializer.Integer("Type", type);
  serializer.String("Name", name);
  serializer.Integer("Optimal operation size", optimal_operation_size);
}

void StorageDeviceReadRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Offset on device", offset_on_device);
  serializer.Integer("Offset in buffer", offset_in_buffer);
  serializer.Integer("Bytes to copy", bytes_to_copy);
  serializer.Serializable("Shared memory", buffer);
}

}  // namespace devices
}  // namespace perception
