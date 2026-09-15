#ifndef TEST
// Copyright 2024 Google LLC
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

#include "syscall/syscall.h"

#include "common/kernel_string.h"
#include "diagnostics/stack_trace.h"
#include "hardware/io.h"
#include "output/text_terminal.h"
#include "processes/process.h"
#include "scheduling/cpu_core.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"
#include "syscall/ipc_syscalls.h"
#include "syscall/memory_syscalls.h"
#include "syscall/process_syscalls.h"
#include "syscall/shared_memory_syscalls.h"
#include "syscall/syscall.asm.h"
#include "syscall/syscall_context.h"
#include "syscall/syscalls.h"
#include "syscall/system_syscalls.h"
#include "syscall/thread_syscalls.h"

using hardware::kIa32FmaskMsr;
using hardware::kLstarMsr;
using hardware::kStarMsr;
using hardware::WriteModelSpecificRegister;
using output::NumberFormat;
using output::print;
using scheduling::CurrentlyExecutingThreadRegs;
using scheduling::kKernelCodeSelector;
using scheduling::kKernelDataSelector;
using scheduling::RunningThread;
using syscall::GetSystemCallName;
using syscall::Syscall;
using syscall::SyscallContext;

namespace {

// STAR MSR segment base for kernel (CS=0x08, SS=0x10) and user (SS=0x18,
// CS=0x20). In 64-bit mode, bits 32-47 provide kernel CS (SS is CS+8).
constexpr uint64 kKernelSegmentBase = static_cast<uint64>(kKernelCodeSelector)
                                      << 32;

// In 64-bit mode, bits 48-63 provide user CS/SS base (SS is base+8, CS is
// base+16). With base=0x10, user SS is 0x18 and user CS is 0x20.
constexpr uint64 kUserSegmentBase = static_cast<uint64>(kKernelDataSelector)
                                    << 48;

// Flags in IA32_FMASK, which are cleared from RFLAGS on syscall entry. Anything
// left set here is inherited from the calling process into Ring 0.

// Interrupt flag. The kernel runs with interrupts disabled throughout.
constexpr uint32 kInterruptFlagMask = 0x0200;

// Trap flag. If left set, a process can single step into Ring 0 and raise a
// #DB there, which is treated as a fatal in-kernel exception.
constexpr uint32 kTrapFlagMask = 0x0100;

// Direction flag. If left set, compiler emitted string operations in kernel
// handlers run backwards.
constexpr uint32 kDirectionFlagMask = 0x0400;

// Alignment check flag, which would fault the kernel on unaligned accesses.
constexpr uint32 kAlignmentCheckFlagMask = 0x40000;

// The full set of flags cleared on entering a syscall.
constexpr uint32 kSyscallFlagMask = kInterruptFlagMask | kTrapFlagMask |
                                    kDirectionFlagMask |
                                    kAlignmentCheckFlagMask;

// How many unimplemented syscalls are reported before reporting stops. Each
// report writes a register and stack dump over a serial port while holding the
// terminal lock with interrupts disabled, so an unlimited number of them is an
// easy way for any process to stall the whole system.
constexpr size_t kMaxUnimplementedSyscallReports = 32;

// How many unimplemented syscalls have been reported so far.
size_t g_unimplemented_syscall_reports = 0;

// Handles unimplemented system calls.
void HandleUnimplementedSyscall(int syscall_number, SyscallContext& context) {
  size_t reports = __atomic_fetch_add(&g_unimplemented_syscall_reports, 1,
                                      __ATOMIC_RELAXED);
  if (reports >= kMaxUnimplementedSyscallReports) return;

  print << "Syscall " << GetSystemCallName(syscall_number) << " ("
        << NumberFormat::Decimal << syscall_number;
  if (context.has_thread()) {
    print << ") from " << context.process()->name << " ("
          << context.process()->pid;
  }
  print << ") is unimplemented.\n";
  diagnostics::PrintRegistersAndStackTrace();

  if (reports + 1 == kMaxUnimplementedSyscallReports)
    print << "Further unimplemented syscalls will not be reported.\n";
}

}  // namespace

namespace syscall {

void InitializeSystemCalls() {
  WriteModelSpecificRegister(kStarMsr, kKernelSegmentBase | kUserSegmentBase);
  WriteModelSpecificRegister(kLstarMsr,
                             reinterpret_cast<size_t>(syscall_entry));
  // Disable interrupts and clear the inheritable flags during syscalls.
  WriteModelSpecificRegister(kIa32FmaskMsr, kSyscallFlagMask);
}

}  // namespace syscall

namespace syscall {

extern "C" void SyscallHandler(int syscall_number) {
  if (RunningThread()) RunningThread()->in_syscall = true;

  SyscallContext context(*CurrentlyExecutingThreadRegs(), RunningThread());

  switch (static_cast<Syscall>(syscall_number)) {
    default:
      HandleUnimplementedSyscall(syscall_number, context);
      break;

    // Debugging & Diagnostics
    case Syscall::PrintDebugCharacter:
      PrintDebugCharacter(context);
      break;
    case Syscall::PrintRegistersAndStack:
      PrintRegistersAndStack(context);
      break;

    case Syscall::CreateThread:
      CreateThread(context);
      break;
    case Syscall::GetThisThreadId:
      GetThisThreadId(context);
      break;
    case Syscall::SleepThisThread:
      SleepThisThread(context);
      break;
    case Syscall::SleepThread:
      SleepThread(context);
      break;
    case Syscall::WakeThread:
      WakeThread(context);
      break;
    case Syscall::WaitAndSwitchToThread:
      // In original implementation, WaitAndSwitchToThread falls through / not
      // implemented.
      HandleUnimplementedSyscall(syscall_number, context);
      break;
    case Syscall::TerminateThisThread:
      TerminateThisThread(context);
      break;
    case Syscall::TerminateThread:
      TerminateThread(context);
      break;
    case Syscall::SetThreadSegment:
      SetThreadSegment(context);
      break;
    case Syscall::SetThreadSegmentExtended:
      SetThreadSegmentExtended(context);
      break;
    case Syscall::SetAddressToClearOnThreadTermination:
      SetAddressToClearOnThreadTermination(context);
      break;
    case Syscall::SetThreadPriority:
      SetThreadPriority(context);
      break;

    // Process Management
    case Syscall::GetThisProcessId:
      GetThisProcessId(context);
      break;
    case Syscall::TerminateThisProcess:
      TerminateThisProcess(context);
      break;
    case Syscall::TerminateProcess:
      TerminateProcess(context);
      break;
    case Syscall::GetProcesses:
      GetProcesses(context);
      break;
    case Syscall::GetNameOfProcess:
      GetNameOfProcess(context);
      break;
    case Syscall::NotifyWhenProcessDisappears:
      NotifyWhenProcessDisappears(context);
      break;
    case Syscall::StopNotifyingWhenProcessDisappears:
      StopNotifyingWhenProcessDisappears(context);
      break;
    case Syscall::CreateProcess:
      CreateProcess(context);
      break;
    case Syscall::SetChildProcessMemoryPages:
      SetChildProcessMemoryPages(context);
      break;
    case Syscall::StartExecutionProcess:
      StartExecutionProcess(context);
      break;
    case Syscall::DestroyChildProcess:
      DestroyChildProcess(context);
      break;
    case Syscall::SetFocusedProcess:
      SetFocusedProcess(context);
      break;

    // Memory Management
    case Syscall::AllocateMemoryPages:
      AllocateMemoryPages(context);
      break;
    case Syscall::AllocateMemoryPagesBelowPhysicalBase:
      AllocateMemoryPagesBelowPhysicalBase(context);
      break;
    case Syscall::ReleaseMemoryPages:
      ReleaseMemoryPages(context);
      break;
    case Syscall::MapPhysicalMemory:
      MapPhysicalMemory(context);
      break;
    case Syscall::GetPhysicalAddressOfVirtualAddress:
      GetPhysicalAddressOfVirtualAddress(context);
      break;
    case Syscall::GetSystemMetrics:
      GetSystemMetrics(context);
      break;
    case Syscall::GetProcessHealthMetrics:
      GetProcessHealthMetrics(context);
      break;
    case Syscall::SetMemoryAccessRights:
      SetMemoryAccessRights(context);
      break;

    // Shared Memory Management
    case Syscall::CreateSharedMemory:
      CreateSharedMemory(context);
      break;
    case Syscall::JoinSharedMemory:
      JoinSharedMemory(context);
      break;
    case Syscall::JoinChildProcessInSharedMemory:
      JoinChildProcessInSharedMemory(context);
      break;
    case Syscall::LeaveSharedMemory:
      LeaveSharedMemory(context);
      break;
    case Syscall::GetSharedMemoryDetails:
      GetSharedMemoryDetails(context);
      break;
    case Syscall::MovePageIntoSharedMemory:
      MovePageIntoSharedMemory(context);
      break;
    case Syscall::GrantPermissionToAllocateIntoSharedMemory:
      GrantPermissionToAllocateIntoSharedMemory(context);
      break;
    case Syscall::IsSharedMemoryPageAllocated:
      IsSharedMemoryPageAllocated(context);
      break;
    case Syscall::GetSharedMemoryPagePhysicalAddress:
      GetSharedMemoryPagePhysicalAddress(context);
      break;
    case Syscall::GrowSharedMemory:
      GrowSharedMemory(context);
      break;
    case Syscall::RegisterSharedMemoryEvent:
      RegisterSharedMemoryEvent(context);
      break;
    case Syscall::UnregisterSharedMemoryEvent:
      UnregisterSharedMemoryEvent(context);
      break;
    case Syscall::TriggerSharedMemoryEvent:
      TriggerSharedMemoryEvent(context);
      break;

    // Inter-Process Communication (IPC) & Services
    case Syscall::SendMessage:
      SendMessage(context);
      break;
    case Syscall::PollForMessage:
      PollForMessage(context);
      break;
    case Syscall::SleepForMessage:
      SleepForMessage(context);
      break;
    case Syscall::SetSystemMessageHandlers:
      SetSystemMessageHandlers(context);
      break;
    case Syscall::RegisterService:
      RegisterService(context);
      break;
    case Syscall::UnregisterService:
      UnregisterService(context);
      break;
    case Syscall::GetServices:
      GetServices(context);
      break;
    case Syscall::GetNameOfService:
      GetNameOfService(context);
      break;
    case Syscall::NotifyWhenServiceAppears:
      NotifyWhenServiceAppears(context);
      break;
    case Syscall::StopNotifyingWhenServiceAppears:
      StopNotifyingWhenServiceAppears(context);
      break;
    case Syscall::NotifyWhenServiceDisappears:
      NotifyWhenServiceDisappears(context);
      break;
    case Syscall::StopNotifyingWhenServiceDisappears:
      StopNotifyingWhenServiceDisappears(context);
      break;

    // Hardware, Timers & System
    case Syscall::GetMultibootModule:
      GetMultibootModule(context);
      break;
    case Syscall::GetMultibootFramebufferInformation:
      GetMultibootFramebufferInformation(context);
      break;
    case Syscall::GetAcpiDetails:
      GetAcpiDetails(context);
      break;
    case Syscall::RegisterMessageToSendOnInterrupt:
      RegisterMessageToSendOnInterrupt(context);
      break;
    case Syscall::UnregisterMessageToSendOnInterrupt:
      UnregisterMessageToSendOnInterrupt(context);
      break;
    case Syscall::SendMessageAfterXMicroseconds:
      SendMessageAfterXMicroseconds(context);
      break;
    case Syscall::SendMessageAtTimestamp:
      SendMessageAtTimestamp(context);
      break;
    case Syscall::GetCurrentTimestamp:
      GetCurrentTimestamp(context);
      break;
    case Syscall::GetTimeInfo:
      GetTimeInfo(context);
      break;
    case Syscall::SetTimeInfo:
      SetTimeInfo(context);
      break;
    case Syscall::RegisterMessageForWhenTimeInfoChanges:
      RegisterMessageForWhenTimeInfoChanges(context);
      break;
    case Syscall::EnableProfiling:
      EnableProfiling(context);
      break;
    case Syscall::DisableAndOutputProfiling:
      DisableAndOutputProfiling(context);
      break;
    case Syscall::SetThatProcessCaresAboutCpuTracking:
      SetThatProcessCaresAboutCpuTracking(context);
      break;
  }
}

extern "C" bool RescheduleWithIretq() {
  ::scheduling::Thread* thread = RunningThread();
  // Nothing is running on this core, so the idle registers must be entered
  // with an iretq.
  if (thread == nullptr) return true;

  // The scheduler clears 'in_syscall' on any thread it switches away from, so
  // the flag still being set means the thread that invoked the syscall is the
  // thread being returned to and the fast sysret path can be taken.
  bool resuming_calling_thread = thread->in_syscall;
  thread->in_syscall = false;
  return !resuming_calling_thread;
}

}  // namespace syscall

#endif  // TEST
