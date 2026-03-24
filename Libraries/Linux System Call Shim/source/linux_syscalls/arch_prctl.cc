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

#include "linux_syscalls/arch_prctl.h"

#include <errno.h>

#include "perception/threads.h"

namespace perception {
namespace linux_syscalls {

long arch_prctl(int code, unsigned long addr) {
  if (code == 0x1002) { // ARCH_SET_FS
    ::perception::SetThreadSegments(addr, true, 0, false);
    return 0;
  } else if (code == 0x1001) { // ARCH_SET_GS
    ::perception::SetThreadSegments(0, false, addr, true);
    return 0;
  }
  return -EINVAL;
}

}  // namespace linux_syscalls
}  // namespace perception
