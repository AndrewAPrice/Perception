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
#pragma once

#include <memory>

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {
class Serializer;
}

class ReadFileRequest : public serialization::Serializable {
 public:
  uint64 offset_in_file;
  uint64 offset_in_destination_buffer;
  uint64 bytes_to_copy;
  std::shared_ptr<SharedMemory> buffer_to_copy_into;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest
    : public serialization::Serializable {
 public:
  std::shared_ptr<SharedMemory> buffer;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                                                \
  X(1, Close, void, void)                                             \
  X(2, Read, void, ReadFileRequest)                                   \
  X(3, GrantStorageDevicePermissionToAllocateSharedMemoryPages, void, \
    GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest)

DEFINE_PERCEPTION_SERVICE(File, "perception.File", METHOD_LIST)

#undef METHOD_LIST

}  // namespace perception