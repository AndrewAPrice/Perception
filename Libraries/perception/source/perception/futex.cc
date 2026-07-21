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

#include "perception/futex.h"

#include <atomic>
#include <map>
#include <vector>

#include "perception/fibers.h"

namespace perception {
namespace {

std::atomic_flag futex_lock = ATOMIC_FLAG_INIT;

std::map<volatile int*, std::vector<Fiber*>>& FibersSleepingOnAddrs() {
  static std::map<volatile int*, std::vector<Fiber*>> fibers_sleeping_on_addrs;
  return fibers_sleeping_on_addrs;
}

}  // namespace

bool WaitOnFutex(void* address, int value) {
  volatile int* addr = (volatile int*)address;
  if (addr == nullptr || *addr != value) {
    return false;
  }

  while (futex_lock.test_and_set(std::memory_order_acquire)) {
    // Spin
  }
  Fiber* current_fiber = ::perception::GetCurrentlyExecutingFiber();
  FibersSleepingOnAddrs()[addr].push_back(current_fiber);
  futex_lock.clear(std::memory_order_release);

  Sleep();
  return true;
}

void WakeFutex(void* address, int value) {
  volatile int* addr = (volatile int*)address;
  if (addr == nullptr) {
    return;
  }

  std::vector<Fiber*> fibers_to_wake;

  while (futex_lock.test_and_set(std::memory_order_acquire)) {
    // Spin
  }
  auto itr = FibersSleepingOnAddrs().find(addr);
  if (itr != FibersSleepingOnAddrs().end()) {
    if ((int)itr->second.size() <= value) {
      fibers_to_wake = std::move(itr->second);
      FibersSleepingOnAddrs().erase(itr);
    } else {
      fibers_to_wake.assign(itr->second.begin(), itr->second.begin() + value);
      itr->second.erase(itr->second.begin(), itr->second.begin() + value);
    }
  }
  futex_lock.clear(std::memory_order_release);
  for (Fiber* fiber : fibers_to_wake) fiber->WakeUp();
}

}  // namespace perception
