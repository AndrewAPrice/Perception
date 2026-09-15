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

#ifndef TEST
#include "syscall/system_syscalls.h"

#include "diagnostics/profiling.h"
#include "diagnostics/stack_trace.h"
#include "hardware/acpi.h"
#include "interrupts/interrupts.h"
#include "loader/multiboot_modules.h"
#include "output/framebuffer.h"
#include "output/text_terminal.h"
#include "processes/process.h"
#include "scheduling/thread.h"
#include "scheduling/timer.h"

using hardware::PopulateRegistersWithAcpiDetails;
using output::NumberFormat;
using output::PopulateRegistersWithFramebufferDetails;
using output::print;
using output::ScopedPrintSource;
using scheduling::GetCurrentTimestampInMicroseconds;
using scheduling::GetTimeInfo;
using scheduling::RegisterMessageForWhenTimeInfoChanges;
using scheduling::SendMessageToProcessAtMicroseconds;
using scheduling::SetThatProcessCaresAboutCpuTracking;
using scheduling::SetTimeInfo;

namespace syscall {

void PrintDebugCharacter(SyscallContext& context) {
  if (!context.has_thread()) return;

  char c = static_cast<char>(context.arg0());
  int channel = static_cast<int>(context.arg1());
  ScopedPrintSource source(context.process()->pid, context.process()->name,
                           channel);
  print << c;
}

void PrintRegistersAndStack(SyscallContext& context) {
  if (!context.has_thread()) return;

  ScopedPrintSource source(context.process()->pid, context.process()->name, 0);
  print << "Dump requested by PID " << NumberFormat::Decimal
        << context.process()->pid << " (" << context.process()->name
        << ") in TID " << context.thread()->id << '\n';
  diagnostics::PrintRegistersAndStackTrace();
}

void GetMultibootModule(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t module_name[loader::kModuleNameWords] = {0};
  loader::LoadNextMultibootModuleIntoProcess(
      context.process(),
      /*address_and_flags=*/context.registers().rdi,
      /*size=*/context.registers().r15, reinterpret_cast<char*>(module_name));
  context.WriteWords(module_name);
}

void GetMultibootFramebufferInformation(SyscallContext& context) {
  if (!context.has_thread()) return;

  PopulateRegistersWithFramebufferDetails(context.registers());
}

void GetAcpiDetails(SyscallContext& context) {
  if (context.has_thread() && context.process()->is_driver) {
    PopulateRegistersWithAcpiDetails(context.registers());
  }
}

void RegisterMessageToSendOnInterrupt(SyscallContext& context) {
  if (context.has_thread()) {
    interrupts::RegisterMessageToSendOnInterrupt(
        static_cast<int>(context.arg0()), context.process(), context.arg1(),
        context.arg2(), context.arg3());
  }
}

void UnregisterMessageToSendOnInterrupt(SyscallContext& context) {
  if (context.has_thread()) {
    interrupts::UnregisterMessageToSendOnInterrupt(
        static_cast<int>(context.arg0()), context.process(), context.arg1());
  }
}

void SendMessageAfterXMicroseconds(SyscallContext& context) {
  if (context.has_thread()) {
    SendMessageToProcessAtMicroseconds(
        context.process(), context.arg0() + GetCurrentTimestampInMicroseconds(),
        context.arg1());
  }
}

void SendMessageAtTimestamp(SyscallContext& context) {
  if (context.has_thread()) {
    SendMessageToProcessAtMicroseconds(context.process(), context.arg0(),
                                       context.arg1());
  }
}

void GetCurrentTimestamp(SyscallContext& context) {
  if (!context.has_thread()) return;

  context.Return(GetCurrentTimestampInMicroseconds());
}

void GetTimeInfo(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t offset = 0;
  size_t multiplier = 0;
  ::scheduling::GetTimeInfo(offset, multiplier);
  context.SetReturnValues(offset, multiplier);
}

void SetTimeInfo(SyscallContext& context) {
  if (context.has_thread() && context.process()->is_driver) {
    ::scheduling::SetTimeInfo(context.arg0());
  }
}

void RegisterMessageForWhenTimeInfoChanges(SyscallContext& context) {
  if (context.has_thread()) {
    ::scheduling::RegisterMessageForWhenTimeInfoChanges(context.process(),
                                                        context.arg0());
  }
}

void EnableProfiling(SyscallContext& context) {
  // Profiling is system wide, so it is restricted the same way as the other
  // controls that affect every process.
  if (context.has_thread() && context.process()->is_driver) {
    diagnostics::EnableProfiling(context.process());
  }
}

void DisableAndOutputProfiling(SyscallContext& context) {
  if (context.has_thread() && context.process()->is_driver) {
    diagnostics::DisableAndOutputProfiling(context.process());
  }
}

void SetThatProcessCaresAboutCpuTracking(SyscallContext& context) {
  if (context.has_thread()) {
    ::scheduling::SetThatProcessCaresAboutCpuTracking(context.process(),
                                                      context.arg0() != 0);
  }
}

}  // namespace syscall
#endif  // TEST
