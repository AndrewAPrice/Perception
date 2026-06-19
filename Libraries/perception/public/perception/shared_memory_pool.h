// Copyright 2022 Google LLC
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
#include <stack>

#include "perception/memory.h"
#include "perception/shared_memory.h"

namespace perception {

// Shared memory that can be put in a pool to be recycled.
struct PooledSharedMemory {
  std::shared_ptr<::perception::SharedMemory> shared_memory;
};

template <int shared_memory_size>
class SharedMemoryPool {
 public:
  // Gets shared memory. Tries to recycle a previously released shared if
  // possible.
  std::shared_ptr<PooledSharedMemory> GetSharedMemory() {
    std::shared_ptr<PooledSharedMemory> pooled_shared_memory;

    while (lock_.test_and_set(std::memory_order_acquire)) {
      // Spin
    }

    if (!shared_memory_pool_.empty()) {
      pooled_shared_memory = std::move(shared_memory_pool_.top());
      shared_memory_pool_.pop();
    }

    lock_.clear(std::memory_order_release);

    if (!pooled_shared_memory) {
      // There's no shared memory we can recycle. Allocate it outside the lock
      // to avoid blocking other threads or deadlocking fibers.
      pooled_shared_memory = std::make_shared<PooledSharedMemory>();
      pooled_shared_memory->shared_memory = SharedMemory::FromSize(
          shared_memory_size, SharedMemory::kJoinersCanWrite);
      (void)pooled_shared_memory->shared_memory->Join();
    }
    return pooled_shared_memory;
  }

  // Returns the shared memory to the pool.
  void ReleaseSharedMemory(std::shared_ptr<PooledSharedMemory> shared_memory) {
    while (lock_.test_and_set(std::memory_order_acquire)) {
      // Spin
    }
    shared_memory_pool_.push(std::move(shared_memory));
    lock_.clear(std::memory_order_release);
  }

 private:
  // Spinlock protecting the pool.
  std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

  // Shared memory that is allocated but not used.
  std::stack<std::shared_ptr<PooledSharedMemory>> shared_memory_pool_;
};

}  // namespace perception