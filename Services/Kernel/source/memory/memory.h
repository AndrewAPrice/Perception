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

#include "types.h"
#if defined(TEST)
#include <cstring>
#endif

namespace processes {
struct Process;
}

#if !defined(TEST)
// Copies `count` bytes from `src` to `dest`.
extern "C" void* memcpy(void *dest, const void *src, size_t count);

// Sets `count` bytes in `dest` to `val`.
extern "C" void* memset(void *dest, int val, size_t count);
#endif

namespace memory {

// Most pages that a single syscall may operate on, which is 4 GiB worth. Page
// counts come straight from userspace, and the kernel runs with interrupts
// disabled, so an unbounded loop hangs the core outright rather than merely
// being slow. It also keeps `pages * sizeof(size_t)` well clear of overflowing.
// A caller that needs more than this can make repeated calls.
constexpr size_t kMaxPagesPerSyscall = 1 << 20;

// Clears an object to 0.
template <class T>
void Clear(T &object) {
  memset((char *)&object, 0, sizeof(T));
}

// Returns whether the provided address lives within kernel space.
bool IsKernelAddress(size_t address);

// Copies data from the module into the process's memory.
bool CopyKernelMemoryIntoProcess(size_t from_start, size_t to_start,
                                 size_t to_end, processes::Process *process);

// Fills a range of memory in the process with zeroes.
bool ZeroProcessMemory(size_t to_start, size_t to_end, processes::Process *process);

// Calculates the number of pages required to fit this particular number of
// bytes.
size_t PagesThatContainBytes(size_t bytes);

}  // namespace memory


