// Copyright 2026 Google LLC
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

#include "common/kernel_string.h"

#include "memory/memory.h"

namespace common {

void CopyString(const char* source, size_t buffer_size, size_t strlen,
                char* dest) {
  // Guarded, because the clamp below subtracts from the buffer size and would
  // otherwise wrap to SIZE_MAX.
  if (buffer_size == 0) return;

  // Leave room for a null terminator.
  if (strlen >= buffer_size) strlen = buffer_size - 1;

  memcpy(dest, source, strlen);

  dest += strlen;
  memset(dest, '\0', buffer_size - strlen);
}

#ifndef TEST
}  // namespace common

extern "C" {

size_t strlen(const char* str) {
  size_t count = 0;
  while (*str) {
    count++;
    str++;
  }

  return count;
}

}  // extern "C"

namespace common {
#endif

size_t strlen_s(const char* str, size_t max_size) {
  size_t count = 0;
  while (*str && count < max_size) {
    count++;
    str++;
  }

  return count;
}

bool MemoryEquals(const void* a, const void* b, size_t count) {
  const auto* ac = static_cast<const unsigned char*>(a);
  const auto* bc = static_cast<const unsigned char*>(b);

  while (count > 0) {
    if (*ac != *bc) return false;
    ac++;
    bc++;
    count--;
  }

  return true;
}

bool StringsEqual(const char* a, const char* b) {
  if (a == b) return true;
  if (a == nullptr || b == nullptr) return false;
  while (*a && *b) {
    if (*a != *b) return false;
    a++;
    b++;
  }
  return *a == *b;
}

bool WordsEqual(const void* a, const void* b, size_t count) {
  const auto* aw = static_cast<const size_t*>(a);
  const auto* bw = static_cast<const size_t*>(b);
  for (size_t word = 0; word < count; word++)
    if (aw[word] != bw[word]) return false;

  return true;
}

}  // namespace common
