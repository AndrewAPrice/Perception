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

#pragma once

#include <span>

#include "types.h"

namespace perception {

class MemorySpan {
 public:
  MemorySpan(void* data, size_t length);
  MemorySpan();

  bool IsValid() const;
  explicit operator bool() const;

  MemorySpan SubSpan(size_t offset, size_t length);
  const MemorySpan SubSpan(size_t offset, size_t length) const;

  void* operator*();
  const void* operator*() const;

  template <class T>
  T* ToTypeAtOffset(size_t offset) {
    return (T*)*SubSpan(offset, sizeof(T));
  }

  template <class T>
  const T* ToTypeAtOffset(size_t offset) const {
    return (const T*)*SubSpan(offset, sizeof(T));
  }

  template <class T>
  std::span<T> ToTypedArrayAtOffset(size_t offset, size_t count) {
    MemorySpan sub_span = SubSpan(offset, sizeof(T) * count);
    if (!sub_span) return std::span<T>((T*)nullptr, 0);
    return std::span<T>((T*)*sub_span, count);
  }

  template <class T>
  std::span<const T> ToTypedArrayAtOffset(size_t offset, size_t count) const {
    const MemorySpan sub_span = SubSpan(offset, sizeof(T) * count);
    if (!sub_span) return std::span<const T>((const T*)nullptr, 0);
    return std::span<const T>((const T*)*sub_span, count);
  }

  template <class T>
  T* ToType() {
    return ToTypeAtOffset<T>(0);
  }

  template <class T>
  const T* ToType() const {
    return ToTypeAtOffset<T>(0);
  }

  size_t Length() const;

 private:
  void* data_;
  size_t length_;
};

}  // namespace perception
