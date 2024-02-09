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

#include "stack_trace.h"

#include "physical_allocator.h"
#include "process.h"
#include "registers.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_allocator.h"

namespace {

// The maximum number of levels to print up the call stack for a stack trace.
constexpr int kStackTraceDepth = 100;

// Prints a stack trace for the currently running process.
void PrintStackTrace() {
  VirtualAddressSpace* address_space =
      &running_thread->process->virtual_address_space;
  size_t rbp = currently_executing_thread_regs->rbp;
  size_t rip = currently_executing_thread_regs->rip;

  print << "Stack trace:\n " << NumberFormat::Hexidecimal << rip << '\n';

  // Walk up the call stack.
  for (int i = 0; i < kStackTraceDepth; i++) {
    if ((rbp & 7) != 0) {
      // RBP is not aligned, we'll avoid reading the memory address
      // that RBP points to as it could jump across pages boundaries.
      return;
    }

    // Read the RIP.
    size_t rip_address = rbp + 8;
    // Map the page into memory.
    size_t physical_page_addr =
        GetPhysicalAddress(address_space, rip_address, false);
    if (physical_page_addr == OUT_OF_MEMORY) {
      // Doesn't point to valid memory.
      return;
    }
    size_t* memory =
        (size_t*)TemporarilyMapPhysicalMemory(physical_page_addr, 4);
    // Read the new RIP value.
    rip = memory[(rip_address & (PAGE_SIZE - 1)) >> 3];
    print << " ^ " << rip << " Stack base: " << rbp << '\n';

    // Now read new next RBP.
    // Map the page into memory.
    physical_page_addr = GetPhysicalAddress(address_space, rbp, false);
    if (physical_page_addr == OUT_OF_MEMORY) {
      // Doesn't point to valid memory.
      return;
    }
    memory = (size_t*)TemporarilyMapPhysicalMemory(physical_page_addr, 4);
    // Read the new RBP value.
    rbp = memory[(rbp & (PAGE_SIZE - 1)) >> 3];
  }
}

}  // namespace

void PrintRegistersAndStackTrace() {
  PrintRegisters(currently_executing_thread_regs);
  PrintStackTrace();
}
