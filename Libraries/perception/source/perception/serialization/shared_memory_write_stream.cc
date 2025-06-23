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

#include "perception/serialization/shared_memory_write_stream.h"

#include <mutex>

#include "perception/serialization/binary_serializer.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {

SharedMemoryWriteStream::SharedMemoryWriteStream(SharedMemory* shared_memory,
                                                 size_t offset)
    : shared_memory_(shared_memory), offset_(offset) {
  (void)shared_memory->Join();
  shared_memory->Mutex().lock();
  data_ = **shared_memory_;
  shared_memory_size_ = shared_memory_->GetSize();
}

SharedMemoryWriteStream::~SharedMemoryWriteStream() {
  shared_memory_->Mutex().unlock();
}

// Copies data into the stream.
void SharedMemoryWriteStream::CopyDataIntoStream(const void* data,
                                                 size_t size) {
  CopyDataIntoStream(data, size, offset_);
  offset_ += size;
}

// Copies data into the stream at a specific offset. This is isolated and does
// not change 'CurrentOffset'.
void SharedMemoryWriteStream::CopyDataIntoStream(const void* data, size_t size,
                                                 size_t offset) {
  if (offset + size > shared_memory_size_) {
    shared_memory_->Mutex().unlock();
    if (!shared_memory_->Grow(offset + size)) return;
    shared_memory_->Mutex().lock();

    data = **shared_memory_;
    shared_memory_size_ = shared_memory_->GetSize();
  }

  if (data == nullptr) return;

  memcpy((void*)((size_t)data_ + offset), data, size);
}

// Skip forward the current offset.
void SharedMemoryWriteStream::SkipForward(size_t size) { offset_ += size; }

// The current offset in the stream.
size_t SharedMemoryWriteStream::CurrentOffset() { return offset_; }

void SerializeToSharedMemory(const Serializable& object,
                             SharedMemory& shared_memory, size_t offset) {
  (void)shared_memory.Join();
  if (shared_memory.GetSize() == 0) return;

  SharedMemoryWriteStream write_stream(&shared_memory, offset);
  SerializeIntoStream(object, write_stream);
}

}  // namespace serialization
}  // namespace perception