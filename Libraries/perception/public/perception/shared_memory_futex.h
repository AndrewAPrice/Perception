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

#include <atomic>

#include "perception/shared_memory.h"
#include "types.h"

namespace perception {

// A 3-state shared-memory futex that avoids system calls when uncontended.
class SharedMemoryFutex {
 public:
  // Acquires the lock at `offset` inside `shared_memory`.
  static void Lock(SharedMemory& shared_memory, size_t offset,
                   std::atomic<uint32>& state);

  // Releases the lock at `offset` inside `shared_memory`.
  static void Unlock(SharedMemory& shared_memory, size_t offset,
                     std::atomic<uint32>& state);
};

}  // namespace perception
