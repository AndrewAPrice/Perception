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

#include <types.h>

#include <functional>
#include <string>
#include <vector>

#include "perception/serialization/read_stream.h"

namespace perception {

class SharedMemory;

namespace serialization {

class Serializable;

class MemoryReadStream : public ReadStream {
 public:
  MemoryReadStream(const void* data, size_t size);

  virtual void CopyDataOutOfStream(void* data, size_t size) override;

  virtual bool ContainsAtLeast(size_t bytes) override;

  virtual void SkipForward(size_t size) override;
  virtual void ReadSubStream(size_t size,
                             const std::function<void(ReadStream& sub_stream)>&
                                 on_sub_stream) override;

 private:
  const void* CurrentDataPointer();

  const void* data_;
  size_t size_;
  size_t current_offset_;
};

// Deserializes a serializable from an area of memory.
void DeserializeFromMemory(Serializable& object, const void* data, size_t size);

// Deserializes a serializable from a byte vector.
void DeserializeFromByteVector(Serializable& object,
                               const std::vector<std::byte>& data);

// Deserializes a serializable from shared memory.
void DeserializeFromSharedMemory(Serializable& object,
                                 SharedMemory& shared_memory, size_t offset,
                                 size_t size);

// Deserializes a serializable to a default state.
void DeserializeToEmpty(Serializable& object);

}  // namespace serialization
}  // namespace perception
