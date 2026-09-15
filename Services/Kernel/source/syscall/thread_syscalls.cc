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
#include "syscall/thread_syscalls.h"

#include "interrupts/interrupts.asm.h"
#include "ipc/shared_memory_manager.h"
#include "processes/process.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"
#include "scheduling/thread_state.h"

using scheduling::CreateThread;
using scheduling::DestroyThread;
using scheduling::GetThreadFromTid;
using scheduling::kUserDataSelector;
using scheduling::kUserCodeSelector;
using scheduling::kUserRpl;
using scheduling::ScheduleThread;
using scheduling::SetThreadPriority;
using scheduling::SetThreadSegment;
using scheduling::SetThreadSegments;
using scheduling::Thread;
using scheduling::ThreadPriority;
using scheduling::ThreadState;
using scheduling::UnscheduleThread;

namespace {

// Sentinel message ID indicating no event or message is available.
constexpr size_t kIdForNoEvents = 0xFFFFFFFFFFFFFFFF;

}  // namespace

namespace syscall {

void CreateThread(SyscallContext& context) {
  Thread* running = context.thread();
  if (running == nullptr) return;

  Thread* new_thread = ::scheduling::CreateThread(
      running->process, context.arg0(), context.arg1(), context.arg2(),
      context.arg3());
  if (new_thread == nullptr) {
    context.Return(0);
    return;
  }

  context.Return(new_thread->id);
  ScheduleThread(new_thread);
}

void GetThisThreadId(SyscallContext& context) {
  Thread* running = context.thread();
  if (running != nullptr) context.Return(running->id);
}

void SleepThisThread(SyscallContext& context) {
  Thread* running = context.thread();
  if (running == nullptr) return;

  if (running->wake_signal_pending) {
    running->wake_signal_pending = false;
  } else {
    running->registers.cs = kUserCodeSelector | kUserRpl;
    running->registers.ss = kUserDataSelector | kUserRpl;
    // Unscheduling the running thread schedules the next thread to run on this
    // core.
    UnscheduleThread(running);
    JumpIntoThread();
  }
}

void SleepThread(SyscallContext& context) {
  size_t target_tid = context.arg0();
  if (context.thread() != nullptr && context.thread()->id == target_tid) {
    SleepThisThread(context);
    return;
  }
  processes::Process* process = context.process();
  if (process == nullptr) return;
  Thread* target = GetThreadFromTid(process, target_tid);
  if (target != nullptr) UnscheduleThread(target, ThreadState::Halted);
}

void WakeThread(SyscallContext& context) {
  Thread* running = context.thread();
  if (running == nullptr) return;

  Thread* thread = GetThreadFromTid(running->process, context.arg0());
  if (thread == nullptr || thread->is_terminated()) return;

  if (thread->is_awake()) {
    thread->wake_signal_pending = true;
  } else {
    if (thread->is_waiting_for_message()) {
      thread->process->message_queue.RemoveSleepingThread(*thread);
      thread->registers.rax = kIdForNoEvents;
    } else if (thread->is_waiting_for_shared_memory()) {
      ipc::SharedMemoryManager::Get().RemoveWaitingThread(*thread);
    }
    ScheduleThread(thread);
  }
}

void TerminateThisThread(SyscallContext& context) {
  Thread* running = context.thread();
  if (running == nullptr) return;

  // DestroyThread unschedules the thread, which schedules the next thread to
  // run on this core.
  DestroyThread(running, false);
  JumpIntoThread();
}

void TerminateThread(SyscallContext& context) {
  Thread* running = context.thread();
  if (running == nullptr) return;

  Thread* thread = GetThreadFromTid(running->process, context.arg0());
  if (thread == running) {
    // DestroyThread unschedules the thread, which schedules the next thread to
    // run on this core.
    DestroyThread(running, false);
    JumpIntoThread();
  } else if (thread != nullptr) {
    DestroyThread(thread, false);
  }
}

void SetThreadSegment(SyscallContext& context) {
  Thread* running = context.thread();
  if (running != nullptr) {
    ::scheduling::SetThreadSegment(running, context.arg0());
  }
}

void SetThreadSegmentExtended(SyscallContext& context) {
  Thread* running = context.thread();
  if (running == nullptr) return;

  size_t mask = context.arg2();
  SetThreadSegments(running, context.arg0(), (mask & 1) != 0, context.arg1(),
                    (mask & 2) != 0);
}

void SetAddressToClearOnThreadTermination(SyscallContext& context) {
  Thread* running = context.thread();
  if (running == nullptr) return;

  size_t addr = context.arg0();
  if (addr != 0 &&
      !running->process->virtual_address_space.IsAddressInCorrectSpace(addr)) {
    running->address_to_clear_on_termination = 0;
    return;
  }
  // Align the address to 8 bytes to avoid crossing page boundaries.
  running->address_to_clear_on_termination = addr & (~7L);
}

void SetThreadPriority(SyscallContext& context) {
  Thread* running = context.thread();
  if (running == nullptr) return;

  size_t target_thread_id = context.arg0();
  size_t priority_val = context.arg1();

  Thread* target_thread = nullptr;
  if (target_thread_id == 0 || target_thread_id == running->id) {
    target_thread = running;
  } else {
    target_thread = GetThreadFromTid(running->process, target_thread_id);
  }

  if (target_thread == nullptr) {
    // Invalid thread ID or not part of the caller.
    context.Return(1);
    return;
  }

  if (priority_val > static_cast<size_t>(ThreadPriority::Idle)) {
    // Invalid priority level.
    context.Return(2);
    return;
  }

  ThreadPriority new_priority = static_cast<ThreadPriority>(priority_val);
  if (new_priority == ThreadPriority::InterruptDriver &&
      !running->process->is_driver) {
    // Not a driver and trying to request InterruptDriver priority.
    context.Return(3);
    return;
  }

  ::scheduling::SetThreadPriority(target_thread, new_priority);
  context.Return(0);
}

}  // namespace syscall
#endif  // TEST
