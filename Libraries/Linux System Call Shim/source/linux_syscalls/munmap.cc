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

#include "linux_syscalls/munmap.h"

#include <mutex>
#include <vector>

#include "perception/debug.h"
#include "perception/memory.h"
#include "files.h"

namespace {

std::mutex thread_stacks_mutex;
std::vector<size_t> thread_stacks;

}  // namespace

extern "C" void (*__perception_register_thread_stack_ptr)(void*);

extern "C" void __perception_register_thread_stack(void* stack) {
  std::scoped_lock lock(thread_stacks_mutex);
  ::perception::DebugPrinterSingleton
      << "SHIM: Registering thread stack at " << (size_t)stack << "\n";
  thread_stacks.push_back((size_t)stack);
}

struct StackRegisterInitializer {
  StackRegisterInitializer() {
    __perception_register_thread_stack_ptr = &__perception_register_thread_stack;
  }
} stack_register_initializer;

namespace perception {
namespace linux_syscalls {

long munmap(long addr, long length) {
  ::perception::DebugPrinterSingleton << "SHIM: munmap(" << (size_t)addr
                                      << ", " << (size_t)length << ")\n";
  if (!MaybeCloseMemoryMappedFile((size_t)addr)) {
    // Check if this is a thread stack
    {
      std::scoped_lock lock(thread_stacks_mutex);
      for (auto it = thread_stacks.begin(); it != thread_stacks.end(); ++it) {
        size_t stack_addr = *it;
        if (stack_addr >= (size_t)addr && stack_addr < (size_t)addr + (size_t)length) {
          ::perception::DebugPrinterSingleton
              << "SHIM: Skipping munmap of thread stack at " << (size_t)addr
              << " size " << (size_t)length << "\n";
          thread_stacks.erase(it);
          return 0; // Skip unmapping
        }
      }
    }

    // Not a thread stack, just normal memory to release.
    ReleaseMemoryPages((void*)addr, (size_t)length / kPageSize);
  }
  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
