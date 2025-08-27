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

#include "perception/serialization/write_stream.h"

namespace perception {

class SharedMemory;

namespace serialization {

class Serializable;

class SharedMemoryWriteStream : public WriteStream {
 public:
  SharedMemoryWriteStream(SharedMemory* shared_memory, size_t offset);

  virtual ~SharedMemoryWriteStream();

  // Copies data into the stream.
  virtual void CopyDataIntoStream(const void* data, size_t size) override;

  // Copies data into the stream at a specific offset. This is isolated and does
  // not change 'CurrentOffset'.
  virtual void CopyDataIntoStream(const void* data, size_t size,
                                  size_t offset) override;

  // Skip forward the current offset.
  virtual void SkipForward(size_t size) override;

  // The current offset in the stream.
  virtual size_t CurrentOffset() override;

 private:
  SharedMemory* shared_memory_;
  void* data_;
  size_t offset_;
  size_t shared_memory_size_;
};

// Serializable to shared memory. Grows the shared memory if it's not large
// enough. Returns how many bytes of data were written into memory.
size_t SerializeToSharedMemory(const Serializable& object,
                             SharedMemory& shared_memory, size_t offset);

}  // namespace serialization
}  // namespace perception
