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

#include "exceptions.h"

#include "exceptions.asm.h"
#include "idt.h"
#include "interrupts.asm.h"
#include "interrupts.h"
#include "physical_allocator.h"
#include "process.h"
#include "registers.h"
#include "scheduler.h"
#include "shared_memory.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_allocator.h"

// The maximum number of levels to print up the call stack for a stack trace.
#define STACK_TRACE_DEPTH 100

// Exception number for page faults.
#define PAGE_FAULT 14

// Register the CPU exception interrupts.
void RegisterExceptionInterrupts() {
#ifndef __TEST__
  SetIdtEntry(0, (size_t)isr0, 0x08, 0x8E);
  SetIdtEntry(1, (size_t)isr1, 0x08, 0x8E);
  SetIdtEntry(2, (size_t)isr2, 0x08, 0x8E);
  SetIdtEntry(3, (size_t)isr3, 0x08, 0x8E);
  SetIdtEntry(4, (size_t)isr4, 0x08, 0x8E);
  SetIdtEntry(5, (size_t)isr5, 0x08, 0x8E);
  SetIdtEntry(6, (size_t)isr6, 0x08, 0x8E);
  SetIdtEntry(7, (size_t)isr7, 0x08, 0x8E);
  SetIdtEntry(8, (size_t)isr8, 0x08, 0x8E);
  SetIdtEntry(9, (size_t)isr9, 0x08, 0x8E);
  SetIdtEntry(10, (size_t)isr10, 0x08, 0x8E);
  SetIdtEntry(11, (size_t)isr11, 0x08, 0x8E);
  SetIdtEntry(12, (size_t)isr12, 0x08, 0x8E);
  SetIdtEntry(13, (size_t)isr13, 0x08, 0x8E);
  SetIdtEntry(14, (size_t)isr14, 0x08, 0x8E);
  SetIdtEntry(15, (size_t)isr15, 0x08, 0x8E);
  SetIdtEntry(16, (size_t)isr16, 0x08, 0x8E);
  SetIdtEntry(17, (size_t)isr17, 0x08, 0x8E);
  SetIdtEntry(18, (size_t)isr18, 0x08, 0x8E);
  SetIdtEntry(19, (size_t)isr19, 0x08, 0x8E);
  SetIdtEntry(20, (size_t)isr20, 0x08, 0x8E);
  SetIdtEntry(21, (size_t)isr21, 0x08, 0x8E);
  SetIdtEntry(22, (size_t)isr22, 0x08, 0x8E);
  SetIdtEntry(23, (size_t)isr23, 0x08, 0x8E);
  SetIdtEntry(24, (size_t)isr24, 0x08, 0x8E);
  SetIdtEntry(25, (size_t)isr25, 0x08, 0x8E);
  SetIdtEntry(26, (size_t)isr26, 0x08, 0x8E);
  SetIdtEntry(27, (size_t)isr27, 0x08, 0x8E);
  SetIdtEntry(28, (size_t)isr28, 0x08, 0x8E);
  SetIdtEntry(29, (size_t)isr29, 0x08, 0x8E);
  SetIdtEntry(30, (size_t)isr30, 0x08, 0x8E);
  SetIdtEntry(31, (size_t)isr31, 0x08, 0x8E);
#endif
}

// Messages for each of the exceptions.
const char* exception_messages[] = {
    "Division By Zero",         /* 0 */
    "Debug",                    /* 1 */
    "Non Maskable Interrupt",   /* 2 */
    "Breakpoint",               /* 3 */
    "Into Detected Overflow",   /* 4 */
    "Out of Bounds",            /* 5 */
    "Invalid Opcode",           /* 6 */
    "No Coprocessor",           /* 7 */
    "Double Fault",             /* 8 */
    "Coprocessor Segment",      /* 9 */
    "Bad TSS",                  /* 10 */
    "Segment Not Present",      /* 11 */
    "Stack Fault",              /* 12 */
    "General Protection Fault", /* 13 */
    "Page Fault",               /* 14 */
    "Unknown Interrupt",        /* 15 */
    "Coprocessor Fault",        /* 16 */
    "Alignment Check",          /* 17 */
    "Machine Check",            /* 18 */
    "Reserved",                 /* 19 */
    "Reserved",                 /* 20 */
    "Reserved",                 /* 21 */
    "Reserved",                 /* 22 */
    "Reserved",                 /* 23 */
    "Reserved",                 /* 24 */
    "Reserved",                 /* 25 */
    "Reserved",                 /* 26 */
    "Reserved",                 /* 27 */
    "Reserved",                 /* 28 */
    "Reserved",                 /* 29 */
    "Reserved",                 /* 30 */
    "Reserved"                  /* 31 */
};

namespace {

void PrintStackTrace() {
  VirtualAddressSpace* address_space =
      &running_thread->process->virtual_address_space;
  size_t rbp = currently_executing_thread_regs->rbp;
  size_t rip = currently_executing_thread_regs->rip;

  print << "Stack trace:\n " << NumberFormat::Hexidecimal << rip << '\n';

  // Walk up the call stack.
  for (int i = 0; i < STACK_TRACE_DEPTH; i++) {
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

// The exception handler.
extern "C" void ExceptionHandler(int exception_no, size_t cr2, size_t error_code) {
  if (exception_no == PAGE_FAULT && running_thread != nullptr) {
    if (MaybeHandleSharedMessagePageFault(cr2))
      JumpIntoThread();  // Doesn't return.
  }

  // Output the exception that occured.
  if (exception_no < 32) {
    print << "\nException occured: " << exception_messages[exception_no] << " (" <<
      NumberFormat::Decimal << exception_no << ')';
  } else {
    // This should never trigger, because we haven't registered ourselves
    // for interrupts >= 32.
    print << "\nUnknown exception: " << NumberFormat::Decimal << exception_no;
  }

  bool in_kernel = currently_executing_thread_regs == nullptr ||
    IsKernelAddress(currently_executing_thread_regs->rip);

  if (in_kernel) {
    print << " in kernel";
  } else {
    Process* process = running_thread->process;
    print << " by PID " << process->pid << " (" << process->name << ") in TID " <<
      running_thread->id;
  }

  if (exception_no == PAGE_FAULT) {
    print << " for trying to access " << NumberFormat::Hexidecimal << cr2;
  }
  print << " with error code: " << NumberFormat::Decimal << error_code << '\n';
  PrintRegistersAndStackTrace();

  if (in_kernel) {
  #ifndef __TEST__
      asm("cli");
      asm("hlt");
  #endif
  } else {
    // Terminate the process.
    DestroyProcess(running_thread->process);
    JumpIntoThread();  // Doesn't return.
  }

  /*MarkInterruptHandlerAsLeft();*/
  // return r;
}
