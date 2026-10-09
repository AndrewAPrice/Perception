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
using scheduling::GetThreadAndAcquireReference;
using scheduling::kUserCodeSelector;
using scheduling::kUserDataSelector;
using scheduling::kUserRpl;
using scheduling::ReleaseThreadReference;
using scheduling::RunningThread;
using scheduling::Scheduler;
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

  running->registers.cs = kUserCodeSelector | kUserRpl;
  running->registers.ss = kUserDataSelector | kUserRpl;
  // Unscheduling the running thread schedules the next thread to run on this
  // core unless a wake signal is already pending.
  UnscheduleThread(running);
  if (RunningThread() != running || !running->is_awake()) JumpIntoThread();
}

void SleepThread(SyscallContext& context) {
  size_t target_tid = context.arg0();
  if (context.thread() != nullptr && context.thread()->id == target_tid) {
    SleepThisThread(context);
    return;
  }
  processes::Process* process = context.process();
  if (process == nullptr) return;
  Thread* target = GetThreadAndAcquireReference(process, target_tid);
  if (target == nullptr) return;
  UnscheduleThread(target, ThreadState::Halted);
  ReleaseThreadReference(*target);
}

void WakeThread(SyscallContext& context) {
  Thread* running = context.thread();
  if (running == nullptr) return;

  Thread* thread =
      GetThreadAndAcquireReference(running->process, context.arg0());
  if (thread == nullptr) return;

  bool should_schedule = false;
  {
    containers::InterruptSafeSpinlockGuard msg_guard(
        thread->process->message_queue.lock());
    if (thread->is_waiting_for_message() &&
        thread->process->threads_sleeping_for_message.Remove(thread)) {
      thread->registers.rax = kIdForNoEvents;
      should_schedule = true;
    }
  }

  if (thread->is_waiting_for_shared_memory()) {
    ipc::SharedMemoryManager::Get().RemoveWaitingThread(*thread);
    should_schedule = true;
  }

  if (!should_schedule) {
    containers::InterruptSafeSpinlockGuard sched_guard(Scheduler::Get().lock());
    if (thread->is_terminated()) {
      ReleaseThreadReference(*thread);
      return;
    }
    if (thread->is_awake()) {
      thread->wake_signal_pending = true;
    } else {
      should_schedule = true;
    }
  }

  if (should_schedule) ScheduleThread(thread);
  ReleaseThreadReference(*thread);
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

  size_t target_tid = context.arg0();
  if (target_tid == running->id) {
    // DestroyThread unschedules the thread, which schedules the next thread to
    // run on this core.
    DestroyThread(running, false);
    JumpIntoThread();
    return;
  }

  Thread* thread = GetThreadAndAcquireReference(running->process, target_tid);
  if (thread == nullptr) return;
  DestroyThread(thread, false);
  ReleaseThreadReference(*thread);
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
  bool acquired_ref = false;
  if (target_thread_id == 0 || target_thread_id == running->id) {
    target_thread = running;
  } else {
    target_thread =
        GetThreadAndAcquireReference(running->process, target_thread_id);
    acquired_ref = (target_thread != nullptr);
  }

  if (target_thread == nullptr) {
    // Invalid thread ID or not part of the caller.
    context.Return(1);
    return;
  }

  if (priority_val > static_cast<size_t>(ThreadPriority::Idle)) {
    // Invalid priority level.
    if (acquired_ref) ReleaseThreadReference(*target_thread);
    context.Return(2);
    return;
  }

  ThreadPriority new_priority = static_cast<ThreadPriority>(priority_val);
  if (new_priority == ThreadPriority::InterruptDriver &&
      !running->process->is_driver) {
    // Not a driver and trying to request InterruptDriver priority.
    if (acquired_ref) ReleaseThreadReference(*target_thread);
    context.Return(3);
    return;
  }

  ::scheduling::SetThreadPriority(target_thread, new_priority);
  if (acquired_ref) ReleaseThreadReference(*target_thread);
  context.Return(0);
}

}  // namespace syscall
#endif  // TEST
