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

#include "linux_syscalls/mprotect.h"

#include <sys/mman.h>

#include <iostream>

#include "perception/memory.h"

using ::perception::SetMemoryAccessRights;

namespace perception {
namespace linux_syscalls {

long mprotect(void* addr, size_t len, int prot) {
  size_t pages = (len + kPageSize - 1) / kPageSize;
  if ((prot & PROT_READ) == 0) {
    std::cout << "Ignoring mprotect called without PROT_READ" << std::endl;
    return 0;
  }

  bool can_write = prot & PROT_WRITE;
  bool can_execute = prot & PROT_EXEC;
  SetMemoryAccessRights(addr, pages, can_write, can_execute);
  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
