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

#include "perception/serialization/vector_write_stream.h"

#include <types.h>

#include <functional>
#include <vector>

#include "perception/serialization/binary_serializer.h"
#include "perception/serialization/write_stream.h"

namespace perception {
namespace serialization {

VectorWriteStream::VectorWriteStream(std::vector<std::byte>* data)
    : data_(data) {
  data_->clear();
}

// Copies data into the stream.
void VectorWriteStream::CopyDataIntoStream(const void* data,
                                                   size_t size) {
  for (auto byte_data = (std::byte*)data; size > 0; size--, byte_data++)
    data_->push_back(*byte_data);
}

// Copies data into the stream at a specific offset. This is isolated and does
// not change 'CurrentOffset'.
void VectorWriteStream::CopyDataIntoStream(const void* data,
                                                   size_t size, size_t offset) {
  size_t max_size = size + offset;
  if (data_->size() < max_size) data_->resize(max_size);

  for (auto byte_data = (std::byte*)data; size > 0;
       size--, byte_data++, offset++)
    (*data_)[offset] = *byte_data;
}

// Skip forward the current offset.
void VectorWriteStream::SkipForward(size_t size) {
  for (; size > 0; size--) data_->push_back(std::byte{0});
}

// The current offset in the stream.
size_t VectorWriteStream::CurrentOffset() { return data_->size(); }

std::vector<std::byte> SerializeToByteVector(Serializable& object) {
  std::vector<std::byte> vector;
  VectorWriteStream write_stream(&vector);
  SerializeIntoStream(object, write_stream);

  return vector;
}

}  // namespace serialization
}  // namespace perception
