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

#include <types.h>

// Swaps the byte order of a 16-bit integer (host <-> big-endian network order).
inline uint16 Swap16BitEndian(uint16 val) { return (val >> 8) | (val << 8); }

// Swaps the byte order of a 32-bit integer (host <-> big-endian network order).
inline uint32 Swap32BitEndian(uint32 val) {
  return ((val >> 24) & 0xff) | ((val << 8) & 0xff0000) |
         ((val >> 8) & 0xff00) | ((val << 24) & 0xff000000);
}
