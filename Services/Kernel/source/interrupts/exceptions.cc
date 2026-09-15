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

#include "interrupts/exceptions.h"

#include "hardware/acpi.h"
#include "interrupts/exceptions.asm.h"
#include "interrupts/idt.h"
#include "interrupts/interrupts.asm.h"
#include "interrupts/interrupts.h"
#include "hardware/io.h"
#include "memory/physical_allocator.h"
#include "scheduling/cpu_core.h"
#include "processes/process.h"
#include "hardware/registers.h"
#include "scheduling/scheduler.h"
#include "ipc/shared_memory.h"
#include "diagnostics/stack_trace.h"
#include "output/text_terminal.h"
#include "scheduling/thread.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"

namespace interrupts {

using hardware::AcpiPowerOff;
using hardware::HasAcpiS5;
using hardware::WriteIO16Bits;
using hardware::WriteIOByte;
using memory::FlushVirtualPage;
using memory::KernelAddressSpace;
using memory::VirtualAddressSpace;
using output::NumberFormat;
using output::print;
using output::ScopedPrintSource;
using processes::AreAnyProcessesRunning;
using processes::DestroyProcess;
using processes::Process;
using scheduling::CurrentlyExecutingThreadRegs;
using scheduling::kKernelCodeSelector;
using scheduling::RunningThread;
using scheduling::ScheduleNextThread;

namespace {

// #define SHUTDOWN_ON_ANY_EXCEPTION

// QEMU default (PIIX4) ACPI PM1a_CNT I/O port.
constexpr uint16 kQemuAcpiPm1ControlPort = 0x604;

// ACPI sleep enable bit and sleep type S5 value for QEMU PIIX4.
constexpr uint16 kAcpiS5SleepCommandPiix4 = 0x2000;

// ACPI sleep enable bit and sleep type S5 value for QEMU Q35.
constexpr uint16 kAcpiS5SleepCommandQ35 = 0x3400;

// Bochs and older QEMU poweroff I/O port.
constexpr uint16 kBochsPowerControlPort = 0xB004;

// Bochs and older QEMU poweroff command.
constexpr uint16 kBochsPowerOffCommand = 0x2000;

// QEMU debug exit I/O port. Requires starting QEMU with:
//   -device isa-debug-exit,iobase=0xf4,iosize=0x04.
constexpr uint16 kQemuDebugExitPort = 0xF4;

// Exit code passed to QEMU debug exit on unhandled exception.
constexpr uint8 kQemuDebugExitCode = 0x10;

void PrintException(bool in_kernel, int exception_no, size_t cr2,
                    size_t error_code) {
  ScopedPrintSource source(in_kernel ? 0 : RunningThread()->process->pid,
                           in_kernel ? "Kernel" : RunningThread()->process->name,
                           1);

  Exception exception = static_cast<Exception>(exception_no);
  // Output the exception that occured.
  if (exception_no < 32) {
    print << "\nException occured: " << GetExceptionName(exception) << " ("
          << NumberFormat::Decimal << exception_no << ')';
  } else {
    // This should never trigger, because interrupts >= 32 haven't been
    // registered.
    print << "\nUnknown exception: " << NumberFormat::Decimal << exception_no;
  }

  if (in_kernel) {
    print << " in kernel";
  } else {
    Process *process = RunningThread()->process;
    print << " by PID " << process->pid << " (" << process->name << ") in TID "
          << RunningThread()->id;
    if (RunningThread()->in_syscall) print << " (during syscall)";
  }

  if (exception == Exception::PageFault)
    print << " for trying to access " << NumberFormat::Hexidecimal << cr2;

  print << " with error code: " << NumberFormat::Decimal << error_code << '\n';
  diagnostics::PrintRegistersAndStackTrace();
  if (exception == Exception::PageFault) {
    // Print the free address ranges to help debug what's happening.
    VirtualAddressSpace &address_space =
        in_kernel ? KernelAddressSpace()
                  : RunningThread()->process->virtual_address_space;
    address_space.PrintFreeAddressRanges();
  }
}

// IDT entry flags for 64-bit interrupt gates (Present, DPL 0).
constexpr uint8 kIdtInterruptGateFlags = 0x8E;

// Array of the 32 CPU exception interrupt handlers.
const size_t kIsrHandlers[32] = {
    reinterpret_cast<size_t>(isr0),  reinterpret_cast<size_t>(isr1),
    reinterpret_cast<size_t>(isr2),  reinterpret_cast<size_t>(isr3),
    reinterpret_cast<size_t>(isr4),  reinterpret_cast<size_t>(isr5),
    reinterpret_cast<size_t>(isr6),  reinterpret_cast<size_t>(isr7),
    reinterpret_cast<size_t>(isr8),  reinterpret_cast<size_t>(isr9),
    reinterpret_cast<size_t>(isr10), reinterpret_cast<size_t>(isr11),
    reinterpret_cast<size_t>(isr12), reinterpret_cast<size_t>(isr13),
    reinterpret_cast<size_t>(isr14), reinterpret_cast<size_t>(isr15),
    reinterpret_cast<size_t>(isr16), reinterpret_cast<size_t>(isr17),
    reinterpret_cast<size_t>(isr18), reinterpret_cast<size_t>(isr19),
    reinterpret_cast<size_t>(isr20), reinterpret_cast<size_t>(isr21),
    reinterpret_cast<size_t>(isr22), reinterpret_cast<size_t>(isr23),
    reinterpret_cast<size_t>(isr24), reinterpret_cast<size_t>(isr25),
    reinterpret_cast<size_t>(isr26), reinterpret_cast<size_t>(isr27),
    reinterpret_cast<size_t>(isr28), reinterpret_cast<size_t>(isr29),
    reinterpret_cast<size_t>(isr30), reinterpret_cast<size_t>(isr31)};

} // namespace

// Register the CPU exception interrupts.
void RegisterExceptionInterrupts() {
  for (size_t i = 0; i < 32; i++) {
    unsigned char ist = 0;
    if (i == 8) {
      ist = 1;  // Double Fault uses dedicated IST1 stack.
    } else if (i == 2) {
      ist = 2;  // NMI uses dedicated IST2 stack.
    }
    SetIdtEntry(static_cast<unsigned char>(i), kIsrHandlers[i],
                kKernelCodeSelector, kIdtInterruptGateFlags, ist);
  }
}

const char *GetExceptionName(Exception exception) {
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

// Emergency shutdown.
void Shutdown() {
  if (HasAcpiS5()) AcpiPowerOff();
  WriteIO16Bits(kQemuAcpiPm1ControlPort, kAcpiS5SleepCommandPiix4);
  WriteIO16Bits(kQemuAcpiPm1ControlPort, kAcpiS5SleepCommandQ35);
  WriteIO16Bits(kBochsPowerControlPort, kBochsPowerOffCommand);
  WriteIOByte(kQemuDebugExitPort, kQemuDebugExitCode);
}

// The exception handler.
extern "C" void ExceptionHandler(int exception_no, size_t cr2,
                                 size_t error_code) {
  Exception exception = static_cast<Exception>(exception_no);
  if (exception == Exception::PageFault && RunningThread() != nullptr) {
    // Check present bit (bit 0). If present (1), this is a page protection violation
    // (e.g. write to read-only page or NX violation), which cannot be resolved by retrying.
    if ((error_code & 1) == 0) {
      if (ipc::MaybeHandleSharedMessagePageFault(cr2)) {
        if (RunningThread() == nullptr) ScheduleNextThread();
        JumpIntoThread();  // Doesn't return.
      }

      if (RunningThread()->process->virtual_address_space.GetPhysicalAddress(
              cr2, false) != kOutOfMemory) {
        FlushVirtualPage(cr2);
        JumpIntoThread();
      }
    }
  }

  bool in_kernel = CurrentlyExecutingThreadRegs() == nullptr ||
                   RunningThread() == nullptr ||
                   ((CurrentlyExecutingThreadRegs()->cs & 3) == 0);
  PrintException(in_kernel, exception_no, cr2, error_code);

#ifdef SHUTDOWN_ON_ANY_EXCEPTION
  Shutdown();
#endif

  if (in_kernel) {
    Shutdown();
    asm volatile("cli");
    for (;;) asm volatile("hlt");
  } else {
    // Terminate the process.
    Process* process = RunningThread()->process;
    RunningThread() = nullptr;
    CurrentlyExecutingThreadRegs() = nullptr;
    DestroyProcess(process);
    if (!AreAnyProcessesRunning()) {
      print << "All processes terminated.\n";
      Shutdown();
    }

    ScheduleNextThread();
    JumpIntoThread();
  }
}

}  // namespace interrupts

#endif  // TEST
