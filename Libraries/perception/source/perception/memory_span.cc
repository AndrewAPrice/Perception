// Copyright 2024 Google LLC
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

#include "perception/memory_span.h"

namespace perception {

MemorySpan::MemorySpan(void *data, size_t length)
    : data_(data), length_(data == nullptr ? 0 : length) {}

MemorySpan::MemorySpan() : data_(nullptr), length_(0) {}

bool MemorySpan::IsValid() const { return length_ > 0; }

MemorySpan::operator bool() const { return IsValid(); }

MemorySpan MemorySpan::SubSpan(size_t offset, size_t length) {
  if (offset + length > length_) return MemorySpan();
  return MemorySpan((void *)((size_t)data_ + offset), length);
}

const MemorySpan MemorySpan::SubSpan(size_t offset, size_t length) const {
  if (offset + length > length_) return MemorySpan();
  return MemorySpan((void *)((size_t)data_ + offset), length);
}

void *MemorySpan::operator*() { return data_; }

const void *MemorySpan::operator*() const { return data_; }

size_t MemorySpan::Length() const { return length_; }

}  // namespace perception
