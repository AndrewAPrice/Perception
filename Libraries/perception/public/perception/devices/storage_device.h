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
#pragma once

#include <string>

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {
class Serializer;
}

namespace devices {

enum class StorageDeviceType : uint8 { OPTICAL };

class StorageDeviceDetails : public serialization::Serializable {
 public:
  // Size of the device, in bytes.
  uint64 size_in_bytes;

  // Is this device writable?
  bool is_writable;

  // The type of storage device this is.
  StorageDeviceType type;

  // The name of the device.
  std::string name;

  // The optimal size for operations, in bytes.
  uint64 optimal_operation_size;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class StorageDeviceReadRequest : public serialization::Serializable {
 public:
  // The offset on the device to start reading from.
  uint64 offset_on_device;

  // The offset in the buffer to start writing to.
  uint64 offset_in_buffer;

  // The number of bytes to copy from the device into the buffer.
  uint64 bytes_to_copy;

  // The shared memory buffer to write to.
  std::shared_ptr<SharedMemory> buffer;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                               \
  X(1, GetDeviceDetails, StorageDeviceDetails, void) \
  X(2, Read, void, StorageDeviceReadRequest)

DEFINE_PERCEPTION_SERVICE(StorageDevice, "perception.devices.StorageDevice",
                          METHOD_LIST)
#undef METHOD_LIST

}  // namespace devices
}  // namespace perception