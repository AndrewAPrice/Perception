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
#pragma once

#if defined(TEST)
#include <cstring>
#endif  // TEST

#include "types.h"

#if !defined(TEST)
extern "C" {
// Measures the size of a string.
size_t strlen(const char* str);
}
#endif

namespace common {

// Copies a string.
void CopyString(const char* source, size_t buffer_size, size_t strlen,
                char* dest);

// Measures the size of a string with a maximum length.
size_t strlen_s(const char* str, size_t max);

// Compares count bytes between two memory buffers. Returns true if they are equal.
bool MemoryEquals(const void* a, const void* b, size_t count);

// Compares two null-terminated strings. Returns true if they are equal.
bool StringsEqual(const char* a, const char* b);

// Compares count 64-bit words between two buffers. Returns true if they are equal.
bool WordsEqual(const void* a, const void* b, size_t count);

}  // namespace common

