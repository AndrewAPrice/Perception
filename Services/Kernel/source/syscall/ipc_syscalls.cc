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
#include "syscall/ipc_syscalls.h"

#include "interrupts/interrupts.asm.h"
#include "ipc/messages.h"
#include "ipc/service.h"
#include "output/text_terminal.h"
#include "processes/process.h"
#include "scheduling/cpu_core.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"

using ipc::GetServiceName;
using ipc::kServiceNameWords;
using ipc::LoadNextMessageIntoThread;
using ipc::NotifyProcessWhenServiceAppears;
using ipc::NotifyProcessWhenServiceDisappears;
using ipc::QueryServices;
using ipc::RegisterService;
using ipc::SendMessageFromThreadSyscall;
using ipc::SleepThreadUntilMessage;
using ipc::StopNotifyingProcessWhenServiceAppearsByMessageId;
using ipc::StopNotifyingProcessWhenServiceDisappears;
using ipc::UnregisterServiceByMessageId;
using scheduling::kUserDataSelector;
using scheduling::kUserCodeSelector;
using scheduling::kUserRpl;

namespace {

// Maximum services returned in a single GetServices query.
constexpr int kMaxServicesReturned = 5;

}  // namespace

namespace syscall {

void SendMessage(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::SendMessageFromThreadSyscall(context.thread());
  }
}

void PollForMessage(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::LoadNextMessageIntoThread(context.thread());
  }
}

void SleepForMessage(SyscallContext& context) {
  if (!context.has_thread()) return;

  context.thread()->registers.cs = kUserCodeSelector | kUserRpl;
  context.thread()->registers.ss = kUserDataSelector | kUserRpl;

  // Sleeping the thread unschedules it, which schedules the next thread to run
  // on this core.
  if (::ipc::SleepThreadUntilMessage(context.thread())) JumpIntoThread();
}

void SetSystemMessageHandlers(SyscallContext& context) {
  if (context.has_thread()) {
    context.process()->futex_wake_message_id = context.arg0();
  }
}

void RegisterService(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t service_name[kServiceNameWords];
  service_name[0] = context.arg0();
  service_name[1] = context.arg1();
  service_name[2] = context.arg2();
  service_name[3] = context.arg3();
  service_name[4] = context.arg4();
  service_name[5] = context.arg5();
  service_name[6] = context.arg6();
  service_name[7] = context.arg_r12();
  service_name[8] = context.arg_r13();

  ::ipc::RegisterService(reinterpret_cast<char*>(service_name),
                         context.process(), context.arg_r15());
}

void UnregisterService(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::UnregisterServiceByMessageId(context.process(), context.arg0());
  }
}

void GetServices(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t service_name[kServiceNameWords];
  service_name[0] = context.arg2();
  service_name[1] = context.arg3();
  service_name[2] = context.arg4();
  service_name[3] = context.arg5();
  service_name[4] = context.arg6();
  service_name[5] = context.arg_r12();
  service_name[6] = context.arg_r13();
  service_name[7] = context.arg_r14();
  service_name[8] = context.arg_r15();

  size_t min_pid = context.arg0();
  size_t min_sid = context.arg1();

  size_t pids[kMaxServicesReturned] = {0};
  size_t sids[kMaxServicesReturned] = {0};
  size_t services_found = ::ipc::QueryServices(
      reinterpret_cast<const char*>(service_name), min_pid, min_sid, pids, sids,
      kMaxServicesReturned);

  context.set_rdi(services_found);
  context.Return(pids[0]);
  context.registers().rbx = sids[0];
  context.registers().rdx = pids[1];
  context.registers().rsi = sids[1];
  context.registers().r8 = pids[2];
  context.registers().r9 = sids[2];
  context.registers().r10 = pids[3];
  context.set_r12(sids[3]);
  context.set_r13(pids[4]);
  context.set_r14(sids[4]);
}

void GetNameOfService(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t pid = context.arg0();
  size_t sid = context.arg1();
  size_t service_name[kServiceNameWords] = {0};
  if (::ipc::GetServiceName(pid, sid, reinterpret_cast<char*>(service_name))) {
    context.set_rdi(1);
    context.WriteWords(service_name);
  } else {
    context.set_rdi(0);
  }
}

void NotifyWhenServiceAppears(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t service_name[kServiceNameWords];
  service_name[0] = context.arg0();
  service_name[1] = context.arg1();
  service_name[2] = context.arg2();
  service_name[3] = context.arg3();
  service_name[4] = context.arg4();
  service_name[5] = context.arg5();
  service_name[6] = context.arg6();
  service_name[7] = context.arg_r12();
  service_name[8] = context.arg_r13();

  ::ipc::NotifyProcessWhenServiceAppears(reinterpret_cast<char*>(service_name),
                                         context.process(), context.arg_r15());
}

void StopNotifyingWhenServiceAppears(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::StopNotifyingProcessWhenServiceAppearsByMessageId(context.process(),
                                                             context.arg0());
  }
}

void NotifyWhenServiceDisappears(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::NotifyProcessWhenServiceDisappears(
        context.process(), /*service_process_id=*/context.arg0(),
        /*service_message_id=*/context.arg1(),
        /*message_id=*/context.arg2());
  }
}

void StopNotifyingWhenServiceDisappears(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::StopNotifyingProcessWhenServiceDisappears(
        context.process(), /*message_id=*/context.arg0());
  }
}

}  // namespace syscall
#endif  // TEST
