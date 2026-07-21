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

#include "../../../../third_party/Libraries/musl/source/internal/futex.h"
#include "perception/debug.h"
#include "perception/futex.h"

namespace perception {
namespace linux_syscalls {

long futex(volatile int* addr, int op, int val, void* ts) {
  // Ignoring the timeout struct (ts) for now.

  // Ignore FUTEX_PRIVATE/FUTEX_CLOCK_REALTIME.
  op &= 15;

  switch (op) {
    case FUTEX_WAIT:
      return WaitOnFutex((void*)addr, val) ? 0 : -EAGAIN;
    case FUTEX_WAKE:
      WakeFutex((void*)addr, val);
      return 0;
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
