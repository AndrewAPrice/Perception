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

#include "linux_syscalls/sched_getaffinity.h"

#include <errno.h>
#include <string.h>

#include "perception/debug.h"

namespace perception {
namespace linux_syscalls {

long sched_getaffinity(long pid, unsigned long cpusetsize, void* mask) {
  perception::DebugPrinterSingleton << "SHIM: sched_getaffinity(" << (size_t)pid
                                    << ", " << (size_t)cpusetsize << ", "
                                    << (size_t)mask << ")\n";
  if (mask == nullptr || cpusetsize < 8) {
    return -EINVAL;
  }
  memset(mask, 0, cpusetsize);
  ((unsigned long*)mask)[0] = 1;
  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
