#ifndef TEST
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

#include "diagnostics/stack_trace.h"

#include "memory/physical_allocator.h"
#include "processes/process.h"
#include "hardware/registers.h"
#include "scheduling/scheduler.h"
#include "output/text_terminal.h"
#include "scheduling/thread.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"

namespace diagnostics {

using hardware::PrintRegisters;
using memory::KernelAddressSpace;
using memory::PageOffset;
using memory::TemporarilyMapPhysicalPages;
using memory::VirtualAddressSpace;
using output::NumberFormat;
using output::print;
using scheduling::CurrentlyExecutingThreadRegs;
using scheduling::RunningThread;

namespace {

// The maximum number of levels to print up the call stack for a stack trace.
constexpr int kStackTraceDepth = 100;

// Reads a 64-bit word from virtual memory using temporary page mapping slot 4.
// Returns false if the address is not mapped.
bool ReadVirtualMemoryWord(VirtualAddressSpace& address_space,
                           size_t virtual_address, size_t& value_out) {
  size_t physical_page =
      address_space.GetPhysicalAddress(virtual_address, false);
  if (physical_page == kOutOfMemory) return false;

  auto* memory =
      static_cast<size_t*>(TemporarilyMapPhysicalPages(physical_page, 4));
  value_out = memory[PageOffset(virtual_address) >> 3];
  return true;
}

// Prints a stack trace for the currently running process.
void PrintStackTrace() {
  if (CurrentlyExecutingThreadRegs() == nullptr) {
    print << "Can't print stack trace because no thread is executing.\n";
    return;
  }
  VirtualAddressSpace& address_space =
      (RunningThread() == nullptr) ? KernelAddressSpace()
                                   : RunningThread()->process->virtual_address_space;
  size_t rbp = CurrentlyExecutingThreadRegs()->rbp;
  size_t rip = CurrentlyExecutingThreadRegs()->rip;

  print << "Stack trace:\n " << NumberFormat::Hexidecimal << rip << '\n';

  // Walk up the call stack.
  for (int i = 0; i < kStackTraceDepth; i++) {
    if ((rbp & 7) != 0) {
      // RBP is not aligned, avoid reading the memory address
      // that RBP points to as it could jump across page boundaries.
      return;
    }

    // Read the return address (RIP).
    if (!ReadVirtualMemoryWord(address_space, rbp + 8, rip)) return;
    print << " ^ " << rip << " Stack base: " << rbp << '\n';

    // Read the next frame pointer (RBP).
    size_t next_rbp = 0;
    if (!ReadVirtualMemoryWord(address_space, rbp, next_rbp)) return;
    if (next_rbp <= rbp) return;
    rbp = next_rbp;
  }
}

}  // namespace

void PrintRegistersAndStackTrace() {
  if (CurrentlyExecutingThreadRegs() != nullptr) {
    PrintRegisters(*CurrentlyExecutingThreadRegs());
    PrintStackTrace();
  } else {
    print << "No currently executing thread to print registers and stack "
             "trace.\n";
  }
}

}  // namespace diagnostics

#endif // TEST
