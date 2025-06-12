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

#include "perception/serialization/memory_read_stream.h"

#include <cmath>
#include <cstring>
#include <functional>

#include "perception/serialization/binary_deserializer.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {

MemoryReadStream::MemoryReadStream(const void* data, size_t size)
    : data_(data), size_(size), current_offset_(0) {}

void MemoryReadStream::CopyDataOutOfStream(void* data, size_t size) {
  if (current_offset_ >= size_) {
    // Beyond the end of the stream. Clear the entire stream.
    std::memset(data, 0, size);
    return;
  }

  size_t remaining_size_in_stream = size_ - current_offset_;
  if (size > remaining_size_in_stream) {
    // Partial read.
    std::memcpy(data, CurrentDataPointer(), remaining_size_in_stream);
    SkipForward(remaining_size_in_stream);

    size_t remaining_size_to_copy = size - remaining_size_in_stream;
    std::memset(static_cast<char*>(data) + remaining_size_in_stream, 0,
                remaining_size_to_copy);
  } else {
    // Read the full amount.
    std::memcpy(data, CurrentDataPointer(), size);
    SkipForward(size);
  }
}

bool MemoryReadStream::ContainsAtLeast(size_t bytes) {
  return current_offset_ + bytes < size_;
}

void MemoryReadStream::SkipForward(size_t size) { current_offset_ += size; }

void MemoryReadStream::ReadSubStream(
    size_t size,
    const std::function<void(ReadStream& sub_stream)>& on_sub_stream) {
  if (current_offset_ >= size_) {
    // Beyond the end of the stream.
    MemoryReadStream substream(nullptr, 0);
    on_sub_stream(substream);
    return;
  }

  size_t remaining_size = size_ - current_offset_;
  size_t substream_size = std::min(size, remaining_size);

  MemoryReadStream substream(CurrentDataPointer(), substream_size);
  on_sub_stream(substream);

  SkipForward(substream_size);
}

const void* MemoryReadStream::CurrentDataPointer() {
  return (void*)((size_t)data_ + current_offset_);
}

void DeserializeFromMemory(Serializable& object, const void* data,
                           size_t size) {
  MemoryReadStream read_stream(data, size);
  DeserializeFromStream(object, read_stream);
}

void DeserializeFromByteVector(Serializable& object,
                               const std::vector<std::byte>& data) {
  DeserializeFromMemory(object, &data[0], data.size());
}

void DeserializeFromSharedMemory(Serializable& object,
                                 SharedMemory& shared_memory) {
  DeserializeFromMemory(object, *shared_memory, shared_memory.GetSize());
}

}  // namespace serialization
}  // namespace perception
