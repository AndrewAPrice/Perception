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

#include "types.h"

namespace perception {

// Flushes a cache line from all CPU levels to RAM.
inline void FlushCacheLine(volatile void* addr) {
#ifdef PERCEPTION
  __asm__ __volatile__("clflush (%0)" ::"r"(addr) : "memory");
#endif
}

// Flushes a virtual memory address range from CPU caches to physical RAM.
// Required for software emulators (like QEMU TCG) to see coherent DMA updates.
inline void FlushRange(volatile void* addr, size_t size) {
#ifdef PERCEPTION
  uint8* ptr = (uint8*)addr;
  // NOTE: The standard x86 cache line is 64 bytes.
  for (size_t i = 0; i < size; i += 64) FlushCacheLine(ptr + i);
  __asm__ __volatile__("mfence" ::: "memory");
#endif
}

}  // namespace perception
