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

#include "core_dump.h"
#include "exceptions.asm.h"
#include "idt.h"
#include "interrupts.asm.h"
#include "interrupts.h"
#include "physical_allocator.h"
#include "process.h"
#include "registers.h"
#include "scheduler.h"
#include "shared_memory.h"
#include "stack_trace.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

namespace {

// On an exception, print a core dump instead of anything else.
constexpr bool kCoreDumpOnException = true;

void PrintException(bool in_kernel, int exception_no, size_t cr2,
                    size_t error_code) {
  if (kCoreDumpOnException && !in_kernel) {
    PrintCoreDump(running_thread->process, running_thread, exception_no, cr2,
                  error_code);
  }
  Exception exception = static_cast<Exception>(exception_no);
  // Output the exception that occured.
  if (exception_no < 32) {
    print << "\nException occured: " << GetExceptionName(exception) << " ("
          << NumberFormat::Decimal << exception_no << ')';
  } else {
    // This should never trigger, because we haven't registered ourselves
    // for interrupts >= 32.
    print << "\nUnknown exception: " << NumberFormat::Decimal << exception_no;
  }

  if (in_kernel) {
    print << " in kernel";
  } else {
    Process* process = running_thread->process;
    print << " by PID " << process->pid << " (" << process->name << ") in TID "
          << running_thread->id;
  }

  if (exception == Exception::PageFault) {
    print << " for trying to access " << NumberFormat::Hexidecimal << cr2;
  }
  print << " with error code: " << NumberFormat::Decimal << error_code << '\n';
  PrintRegistersAndStackTrace();
  if (exception == Exception::PageFault) {
    // Print the free address ranges to help debug what's happening.
    VirtualAddressSpace& address_space =
        in_kernel ? KernelAddressSpace()
                  : running_thread->process->virtual_address_space;
    address_space.PrintFreeAddressRanges();
  }
}

}  // namespace

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

const char* GetExceptionName(Exception exception) {
  switch (exception) {
    case Exception::DivisionByZero:
      return "Division By Zero";
    case Exception::Debug:
      return "Debug";
    case Exception::NonMaskableInterrupt:
      return "Non Maskable Interrupt";
    case Exception::Breakpoint:
      return "Breakpoint";
    case Exception::IntoDetectedOverflow:
      return "Into Detected Overflow";
    case Exception::OutOfBounds:
      return "Out of Bounds";
    case Exception::InvalidOpcode:
      return "Invalid Opcode";
    case Exception::NoCoprocessor:
      return "No Coprocessor";
    case Exception::DoubleFault:
      return "Double Fault";
    case Exception::CoprocessorSegment:
      return "Coprocessor Segment";
    case Exception::BadTSS:
      return "Bad TSS";
    case Exception::SegmentNotPreset:
      return "Segment Not Present";
    case Exception::StackFault:
      return "Stack Fault";
    case Exception::GeneralProtectionFault:
      return "General Protection Fault";
    case Exception::PageFault:
      return "Page Fault";
    case Exception::UnknownInterrupt:
      return "Unknown Interrupt";
    case Exception::CoprocessorFault:
      return "Coprocessor Fault";
    case Exception::AlignmentCheck:
      return "Alignment Check";
    case Exception::MachineCheck:
      return "Machine Check";
    default:
      return "Unknown";
  }
}

// The exception handler.
extern "C" void ExceptionHandler(int exception_no, size_t cr2,
                                 size_t error_code) {
  Exception exception = static_cast<Exception>(exception_no);
  if (exception == Exception::PageFault && running_thread != nullptr) {
    if (MaybeHandleSharedMessagePageFault(cr2))
      JumpIntoThread();  // Doesn't return.
  }

  bool in_kernel = currently_executing_thread_regs == nullptr ||
                   running_thread == nullptr ||
                   IsKernelAddress(currently_executing_thread_regs->rip);
  PrintException(in_kernel, exception_no, cr2, error_code);

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
