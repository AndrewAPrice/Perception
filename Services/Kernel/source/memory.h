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
#ifdef __TEST__
#include <string.h>

#else

class Process;

// Copies `count` bytes from `src` to `dest`.
extern "C" void memcpy(char *dest, const char *src, size_t count);

// Sets `count` bytes in `dest` to `val`.
extern "C" void memset(char *dest, char val, size_t count);
#endif

// Clears an object to 0.
template <class T>
void Clear(T &object) {
  memset((char *)&object, 0, sizeof(T));
}

// Returns whether the provided address lives within kernel space.
bool IsKernelAddress(size_t address);

// Copies data from the module into the process's memory.
bool CopyKernelMemoryIntoProcess(size_t from_start, size_t to_start,
                                 size_t to_end, Process *process);

// Calculates the number of pages required to fit this particular number of
// bytes.
size_t PagesThatContainBytes(size_t bytes);
