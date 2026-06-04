// Copyright 2020 Google LLC
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

#include "linux_syscalls/futex.h"

#include <errno.h>

#include <atomic>
#include <map>
#include <vector>

#include "../../../../third_party/Libraries/musl/source/internal/futex.h"
#include "perception/debug.h"
#include "perception/fibers.h"

namespace perception {
namespace linux_syscalls {
namespace {

std::atomic_flag futex_lock = ATOMIC_FLAG_INIT;

std::map<volatile int*, std::vector<Fiber*>>& FibersSleepingOnAddrs() {
  static std::map<volatile int*, std::vector<Fiber*>> fibers_sleeping_on_addrs;
  return fibers_sleeping_on_addrs;
}

}  // namespace

long futex(volatile int* addr, int op, int val, void* ts) {
  // Ignoring the timeout struct (ts) for now.

  // Ignore FUTEX_PRIVATE/FUTEX_CLOCK_REALTIME.
  op &= 15;

  switch (op) {
    case FUTEX_WAIT: {
      // Sleep if *addr != val.
      if (*addr != val) return -EAGAIN;

      while (futex_lock.test_and_set(std::memory_order_acquire)) {
        // Spin
      }
      Fiber* current_fiber = ::perception::GetCurrentlyExecutingFiber();
      FibersSleepingOnAddrs()[addr].push_back(current_fiber);
      futex_lock.clear(std::memory_order_release);

      ::perception::Sleep();
      return 0;
    }
    case FUTEX_WAKE: {
      // Wake up to 'val' listeners.
      std::vector<Fiber*> fibers_to_wake;

      while (futex_lock.test_and_set(std::memory_order_acquire)) {
        // Spin
      }
      auto itr = FibersSleepingOnAddrs().find(addr);
      if (itr != FibersSleepingOnAddrs().end()) {
        if (itr->second.size() <= val) {
          fibers_to_wake = std::move(itr->second);
          FibersSleepingOnAddrs().erase(itr);
        } else {
          fibers_to_wake.assign(itr->second.begin(), itr->second.begin() + val);
          itr->second.erase(itr->second.begin(), itr->second.begin() + val);
        }
      }
      futex_lock.clear(std::memory_order_release);

      for (Fiber* fiber : fibers_to_wake) fiber->WakeUp();

      return 0;
    }
    case FUTEX_FD:
      perception::DebugPrinterSingleton << "FUTEX_FD not implemented" << '\n';
      break;
    case FUTEX_REQUEUE:
      perception::DebugPrinterSingleton << "FUTEX_REQUEUE not implemented"
                                        << '\n';
      break;
    case FUTEX_CMP_REQUEUE:
      perception::DebugPrinterSingleton << "FUTEX_CMP_REQUEUE not implemented"
                                        << '\n';
      break;
    case FUTEX_WAKE_OP:
      perception::DebugPrinterSingleton << "FUTEX_WAKE_OP not implemented"
                                        << '\n';
      break;
    case FUTEX_LOCK_PI:
      perception::DebugPrinterSingleton << "FUTEX_LOCK_PI not implemented"
                                        << '\n';
      break;
    case FUTEX_UNLOCK_PI:
      perception::DebugPrinterSingleton << "FUTEX_UNLOCK_PI not implemented"
                                        << '\n';
      break;
    case FUTEX_TRYLOCK_PI:
      perception::DebugPrinterSingleton << "FUTEX_TRYLOCK_PI not implemented"
                                        << '\n';
      break;
    case FUTEX_WAIT_BITSET:
      perception::DebugPrinterSingleton << "FUTEX_WAIT_BITSET not implemented"
                                        << '\n';
      break;
    default:
      perception::DebugPrinterSingleton
          << "Unknown Futex syscall operation: " << (size_t)op << '\n';
      break;
  }
  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
