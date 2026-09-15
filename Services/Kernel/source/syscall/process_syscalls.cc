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
#include "syscall/process_syscalls.h"

#include "common/kernel_string.h"
#include "interrupts/interrupts.asm.h"
#include "ipc/messages.h"
#include "processes/process.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"

using processes::CreateChildProcess;
using processes::DestroyChildProcess;
using processes::DestroyProcess;
using processes::GetProcessFromPid;
using processes::GetProcessName;
using processes::kProcessNameWords;
using processes::NotifyProcessOnDeath;
using processes::Process;
using processes::ProcessRef;
using processes::QueryProcesses;
using processes::SetChildProcessMemoryPages;
using processes::StartExecutingChildProcess;
using processes::StopNotifyingProcessOnDeath;
using scheduling::SetFocusedProcess;

namespace {

// Maximum PIDs returned in a single GetProcesses query.
constexpr int kMaxProcessesReturned = 11;

}  // namespace

namespace syscall {

void GetThisProcessId(SyscallContext& context) {
  if (context.has_thread()) context.Return(context.process()->pid);
}

void TerminateThisProcess(SyscallContext& context) {
  if (!context.has_thread()) return;
  // Destroying the process unschedules its threads, which schedules the next
  // thread to run on this core.
  DestroyProcess(context.process());
  JumpIntoThread();
}

void TerminateProcess(SyscallContext& context) {
  if (!context.has_thread()) return;

  ProcessRef process = GetProcessFromPid(context.arg0());
  if (!process) return;

  Process* caller = context.process();
  if (process.get() != caller && process->parent_pid != caller->pid &&
      !caller->can_terminate_processes)
    return;

  bool currently_running_process = process.get() == caller;
  // Destroying the process unschedules its threads, which schedules the next
  // thread to run on this core.
  DestroyProcess(process.get());
  if (currently_running_process) JumpIntoThread();
}

void GetProcesses(SyscallContext& context) {
  size_t process_name[kProcessNameWords];
  context.ExtractWords(process_name);

  size_t pids[kMaxProcessesReturned] = {0};
  size_t processes_found = ::processes::QueryProcesses(
      reinterpret_cast<const char*>(process_name), context.arg_r15(), pids,
      kMaxProcessesReturned);

  context.set_rdi(processes_found);
  context.set_r15(pids[0]);
  context.Return(pids[1]);
  context.registers().rbx = pids[2];
  context.registers().rdx = pids[3];
  context.registers().rsi = pids[4];
  context.registers().r8 = pids[5];
  context.registers().r9 = pids[6];
  context.registers().r10 = pids[7];
  context.registers().r12 = pids[8];
  context.registers().r13 = pids[9];
  context.registers().r14 = pids[10];
}

void GetNameOfProcess(SyscallContext& context) {
  size_t process_name[kProcessNameWords] = {0};
  if (::processes::GetProcessName(context.arg0(),
                                  reinterpret_cast<char*>(process_name))) {
    context.set_rdi(1);
    context.WriteWords(process_name);
  } else {
    context.set_rdi(0);
  }
}

void NotifyWhenProcessDisappears(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t target_pid = context.arg0();
  size_t event_id = context.arg1();

  ProcessRef target = GetProcessFromPid(target_pid);
  if (!target) {
    ipc::SendKernelMessageToProcess(context.process(), event_id, target_pid, 0,
                                    0, 0, 0);
  } else {
    NotifyProcessOnDeath(target.get(), context.process(), event_id);
  }
}

void StopNotifyingWhenProcessDisappears(SyscallContext& context) {
  if (context.has_thread()) {
    StopNotifyingProcessOnDeath(context.process(), context.arg0());
  }
}

void CreateProcess(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t process_name[kProcessNameWords];
  process_name[0] = context.registers().rbx;
  process_name[1] = context.registers().rdx;
  process_name[2] = context.registers().rsi;
  process_name[3] = context.registers().r8;
  process_name[4] = context.registers().r9;
  process_name[5] = context.registers().r10;
  process_name[6] = context.registers().r12;
  process_name[7] = context.registers().r13;
  process_name[8] = context.registers().r14;
  process_name[9] = context.registers().r15;

  Process* child_process = CreateChildProcess(
      context.process(), (char*)process_name, context.arg0());
  context.Return(child_process == nullptr ? 0 : child_process->pid);
}

void SetChildProcessMemoryPages(SyscallContext& context) {
  if (!context.has_thread()) return;

  ProcessRef child_process = GetProcessFromPid(context.arg0());
  ::processes::SetChildProcessMemoryPages(context.process(), child_process.get(), context.arg1(),
                                          context.arg2(), context.arg3());
}

void StartExecutionProcess(SyscallContext& context) {
  if (!context.has_thread()) return;

  ProcessRef child_process = GetProcessFromPid(context.arg0());
  StartExecutingChildProcess(context.process(), child_process.get(), context.arg1(),
                             context.arg2());
}

void DestroyChildProcess(SyscallContext& context) {
  if (!context.has_thread()) return;

  ProcessRef child_process = GetProcessFromPid(context.arg0());
  ::processes::DestroyChildProcess(context.process(), child_process.get());
}

void SetFocusedProcess(SyscallContext& context) {
  if (!context.has_thread()) return;

  if (!context.process()->can_set_focus) {
    context.Return(2);
    return;
  }

  if (context.arg0() == 0) {
    ::scheduling::SetFocusedProcess(nullptr);
    context.Return(0);
    return;
  }

  ProcessRef target = GetProcessFromPid(context.arg0());
  if (!target) {
    context.Return(1);
    return;
  }

  ::scheduling::SetFocusedProcess(target.get());
  context.Return(0);
}


}  // namespace syscall
#endif  // TEST
