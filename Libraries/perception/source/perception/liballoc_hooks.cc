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

#include <atomic>

#include "../../../third_party/liballoc.h"
#include "perception/memory.h"

std::atomic_flag liballoc_spinlock = ATOMIC_FLAG_INIT;

extern "C" int liballoc_lock() {
  while (liballoc_spinlock.test_and_set(std::memory_order_acquire)) {
    // Spin
  }
  return 0;
}

extern "C" int liballoc_unlock() {
  liballoc_spinlock.clear(std::memory_order_release);
  return 0;
}
extern "C" void* liballoc_alloc(size_t pages) {
  return perception::AllocateMemoryPages(pages);
}

extern "C" int liballoc_free(void* ptr, size_t pages) {
  ::perception::ReleaseMemoryPages(ptr, pages);
  return 0;
}
