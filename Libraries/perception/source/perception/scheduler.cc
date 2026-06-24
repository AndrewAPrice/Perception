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

#include "perception/scheduler.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <thread>

#include "perception/fibers.h"
#include "perception/messages.h"
#include "perception/processes.h"
#include "perception/threads.h"

namespace perception {
namespace {

// Queue of fibers that are scheduled to run.
Fiber* first_scheduled_fiber = nullptr;
Fiber* last_scheduled_fiber = nullptr;

// Queue of fibers that are scheduled to run after all other fibers and events
// have been processed.
Fiber* first_late_scheduled_fiber = nullptr;
Fiber* last_late_scheduled_fiber = nullptr;

// Thread-local queues of fibers scheduled on the primary thread.
thread_local __attribute__((tls_model("initial-exec")))
Fiber* primary_first_scheduled_fiber = nullptr;
thread_local __attribute__((tls_model("initial-exec")))
Fiber* primary_last_scheduled_fiber = nullptr;

thread_local __attribute__((tls_model("initial-exec")))
Fiber* primary_first_late_scheduled_fiber = nullptr;
thread_local __attribute__((tls_model("initial-exec")))
Fiber* primary_last_late_scheduled_fiber = nullptr;

struct Spinlock {
  std::atomic_flag flag = ATOMIC_FLAG_INIT;
  void Lock() {
    while (flag.test_and_set(std::memory_order_acquire)) {
    }
  }
  void Unlock() { flag.clear(std::memory_order_release); }
};

struct SpinlockLock {
  Spinlock& lock_;
  SpinlockLock(Spinlock& lock) : lock_(lock) { lock_.Lock(); }
  ~SpinlockLock() { lock_.Unlock(); }
};

Spinlock& GetSchedulerLock() {
  static Spinlock lock;
  return lock;
}

// Dummy message identifier for waking up the primary thread.
MessageId GetWakeUpMessageId() {
  static MessageId wake_up_message_id = []() {
    MessageId id = GenerateUniqueMessageId();
    RegisterRawMessageHandler(id, [](ProcessId, const MessageData&) {});
    return id;
  }();
  return wake_up_message_id;
}

void WakeUpPrimaryThread() {
  MessageData message;
  message.message_id = GetWakeUpMessageId();
  message.metadata = 0;
  SendMessage(GetProcessId(), message);
}

// The fiber to return to when there's no more work to do, instead of sleeping.
// This is the caller of HandleEverything.
thread_local __attribute__((tls_model("initial-exec")))
Fiber* fiber_to_return_to_when_out_of_work = nullptr;
thread_local __attribute__((tls_model("initial-exec")))
Fiber* fiber_to_return_to_after_sleeping_when_out_of_work = nullptr;

// Sleeps until a message. Returns true if a message was received.
bool SleepThreadUntilMessage(ProcessId& senders_pid,
                             MessageData& message_data) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 19;
  volatile register size_t pid_r asm("rbx");
  volatile register size_t message_id_r asm("rax");
  volatile register size_t metadata_r asm("rdx");
  volatile register size_t param1_r asm("rsi");
  volatile register size_t param2_r asm("r8");
  volatile register size_t param3_r asm("r9");
  volatile register size_t param4_r asm("r10");
  volatile register size_t param5_r asm("r12");

  __asm__ __volatile__("syscall\n"
                       : "=r"(pid_r), "=r"(message_id_r), "=r"(metadata_r),
                         "=r"(param1_r), "=r"(param2_r), "=r"(param3_r),
                         "=r"(param4_r), "=r"(param5_r)
                       : "r"(syscall)
                       : "rcx", "r11");

  senders_pid = pid_r;
  message_data.message_id = message_id_r;
  message_data.metadata = metadata_r;
  message_data.param1 = param1_r;
  message_data.param2 = param2_r;
  message_data.param3 = param3_r;
  message_data.param4 = param4_r;
  message_data.param5 = param5_r;

  return message_id_r != 0xFFFFFFFFFFFFFFFF;
#else
  return false;
#endif
}

// Polls for a message, returning false immediately if no message was received.
bool PollForMessage(ProcessId& senders_pid, MessageData& message_data) {
#if defined(PERCEPTION) && !defined(TEST)
  // TODO: Handle metadata
  volatile register size_t syscall asm("rdi") = 18;
  volatile register size_t pid_r asm("rbx");
  volatile register size_t message_id_r asm("rax");
  volatile register size_t metadata_r asm("rdx");
  volatile register size_t param1_r asm("rsi");
  volatile register size_t param2_r asm("r8");
  volatile register size_t param3_r asm("r9");
  volatile register size_t param4_r asm("r10");
  volatile register size_t param5_r asm("r12");

  __asm__ __volatile__("syscall\n"
                       : "=r"(pid_r), "=r"(message_id_r), "=r"(metadata_r),
                         "=r"(param1_r), "=r"(param2_r), "=r"(param3_r),
                         "=r"(param4_r), "=r"(param5_r)
                       : "r"(syscall)
                       : "rcx", "r11");

  senders_pid = pid_r;
  message_data.message_id = message_id_r;
  message_data.metadata = metadata_r;
  message_data.param1 = param1_r;
  message_data.param2 = param2_r;
  message_data.param3 = param3_r;
  message_data.param4 = param4_r;
  message_data.param5 = param5_r;

  return message_id_r != 0xFFFFFFFFFFFFFFFF;
#else
  return false;
#endif
}

}  // namespace

// Defers running a function.
void Defer(const std::function<void()>& function) {
  Scheduler::ScheduleFiber(Fiber::Create(function));
}

void Defer(std::function<void()>&& function) {
  Scheduler::ScheduleFiber(Fiber::Create(std::move(function)));
}

void DeferAfterEvents(const std::function<void()>& function) {
  Scheduler::ScheduleFiberAfterEvents(Fiber::Create(function));
}

void DeferAfterEvents(std::function<void()>&& function) {
  Scheduler::ScheduleFiberAfterEvents(Fiber::Create(std::move(function)));
}

void DeferInParallel(const std::function<void()>& function) {
  std::thread(function).detach();
}

void DeferInParallel(std::function<void()>&& function) {
  std::thread(std::move(function)).detach();
}

void SetFocusedProcess(ProcessId pid) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 66;
  volatile register size_t param asm("rax") = pid;

  __asm__ __volatile__("syscall\n" : : "r"(syscall), "r"(param) : "rcx", "r11");
#endif
}

// Hand over control to the scheduler. This function never returns.
void HandOverControl() {
  if (!IsPrimaryThread()) {
    std::cerr << "Error: HandOverControl called on non-primary thread!"
              << std::endl;
    return;
  }

  if (fiber_to_return_to_when_out_of_work != nullptr ||
      fiber_to_return_to_after_sleeping_when_out_of_work != nullptr) {
    std::cerr << "::perception::HandOverControl can be nested inside of"
              << "::perception::FinishAnyPendingWork or "
              << "WaitForMessagesThenReturn because it will never return."
              << std::endl;
    return;
  }

  // Switch to the next scheduled fiber. This will never return.
  Scheduler::GetNextFiberToRun()->JumpTo();
}

// Runs all fibers, handles all events, then returns when there's
// nothing else to do.
void FinishAnyPendingWork() {
  if (!IsPrimaryThread()) {
    std::cerr << "Error: FinishAnyPendingWork called on non-primary thread!"
              << std::endl;
    return;
  }

  if (fiber_to_return_to_when_out_of_work != nullptr ||
      fiber_to_return_to_after_sleeping_when_out_of_work != nullptr) {
    std::cerr << "::perception::FinishAnyPendingWork and "
              << "WaitForMessagesThenReturn can't be nested." << std::endl;
    return;
  }

  // Remember the current fiber to return to once any pending work is done.
  fiber_to_return_to_when_out_of_work = GetCurrentlyExecutingFiber();

  // Switch to the next scheduled fiber.
  Scheduler::GetNextFiberToRun()->SwitchTo();

  // Once execution has returned here, there is no more work to do.
  fiber_to_return_to_when_out_of_work = nullptr;
}

void WaitForMessagesThenReturn() {
  if (!IsPrimaryThread()) {
    std::cerr
        << "Error: WaitForMessagesThenReturn called on non-primary thread!"
        << std::endl;
    return;
  }

  if (fiber_to_return_to_when_out_of_work != nullptr ||
      fiber_to_return_to_after_sleeping_when_out_of_work != nullptr) {
    std::cerr << "::perception::FinishAnyPendingWork and "
              << "WaitForMessagesThenReturn can't be nested." << std::endl;
    return;
  }

  // Remember the current fiber to return to after any pending work is done.
  fiber_to_return_to_after_sleeping_when_out_of_work =
      GetCurrentlyExecutingFiber();

  // Switch to the next scheduled fiber.
  Scheduler::GetNextFiberToRun()->SwitchTo();

  // Once execution has returned here, there is no more work to do.
  fiber_to_return_to_when_out_of_work = nullptr;
  fiber_to_return_to_after_sleeping_when_out_of_work = nullptr;
}

// Gets the next fiber to run, or sleeps until there is one.
Fiber* Scheduler::GetNextFiberToRun() {
  if (!IsPrimaryThread()) {
    std::cerr
        << "Error: Scheduler::GetNextFiberToRun called on non-primary thread!"
        << std::endl;
    return nullptr;
  }

  while (true) {
    if (primary_first_scheduled_fiber != nullptr) {
      Fiber* fiber = primary_first_scheduled_fiber;
      primary_first_scheduled_fiber =
          primary_first_scheduled_fiber->next_scheduled_fiber_;

      if (primary_first_scheduled_fiber == nullptr) {
        primary_last_scheduled_fiber = nullptr;
      }

      fiber->is_scheduled_to_run_ = false;
      return fiber;
    }

    {
      SpinlockLock lock(GetSchedulerLock());
      if (first_scheduled_fiber != nullptr) {
        Fiber* fiber = first_scheduled_fiber;
        first_scheduled_fiber = first_scheduled_fiber->next_scheduled_fiber_;

        // There are no more fibers scheduled, make sure both the first
        // and last pointers are nullptr.
        if (first_scheduled_fiber == nullptr) {
          last_scheduled_fiber = nullptr;
        }

        // Remove the fiber that is about to execute from the schedule.
        fiber->is_scheduled_to_run_ = false;
        return fiber;
      }
    }

    // Check if there are any messages.
    ProcessId senders_pid;
    MessageData message_data;

    bool has_late_scheduled_fibers =
        (primary_first_late_scheduled_fiber != nullptr);
    if (!has_late_scheduled_fibers) {
      SpinlockLock lock(GetSchedulerLock());
      has_late_scheduled_fibers = (first_late_scheduled_fiber != nullptr);
    }

    if (fiber_to_return_to_when_out_of_work == nullptr &&
        !has_late_scheduled_fibers) {
      if (fiber_to_return_to_after_sleeping_when_out_of_work != nullptr) {
        // Sleep first, then return immediately once there is no more work.
        fiber_to_return_to_when_out_of_work =
            fiber_to_return_to_after_sleeping_when_out_of_work;
      }

      // Nothing is waiting for to finish, so sleep for the next message.
      while (true) {
        if (!SleepThreadUntilMessage(senders_pid, message_data)) {
          // The thread randomly woke without a message. This shouldn't
          // happen.
          continue;
        }

        // Get the fiber to handle this message.
        Fiber* fiber = GetFiberToHandleMessage(senders_pid, message_data);

        // Is there a fiber to handle this message?
        if (fiber != nullptr) return fiber;

        // Re-check scheduled fibers in case we were woken up by a secondary
        // thread.
        if (primary_first_scheduled_fiber != nullptr) break;
        {
          SpinlockLock lock(GetSchedulerLock());
          if (first_scheduled_fiber != nullptr) break;
        }
      }
    } else {
      // Keep looping while there are messages.
      while (PollForMessage(senders_pid, message_data)) {
        // Get the fiber to handle this message.
        Fiber* fiber = GetFiberToHandleMessage(senders_pid, message_data);

        // Is there a fiber to handle this message?
        if (fiber != nullptr) return fiber;
      }

      // There are no messages and there are no other fibers. Run any late
      // fibers now.
      if (primary_first_late_scheduled_fiber != nullptr) {
        Fiber* fiber = primary_first_late_scheduled_fiber;
        primary_first_late_scheduled_fiber =
            primary_first_late_scheduled_fiber->next_scheduled_fiber_;

        if (primary_first_late_scheduled_fiber == nullptr)
          primary_last_late_scheduled_fiber = nullptr;

        fiber->is_scheduled_to_run_ = false;
        return fiber;
      }

      {
        SpinlockLock lock(GetSchedulerLock());
        if (first_late_scheduled_fiber != nullptr) {
          Fiber* fiber = first_late_scheduled_fiber;
          first_late_scheduled_fiber =
              first_late_scheduled_fiber->next_scheduled_fiber_;

          // There are no more fibers scheduled, make sure both the first
          // and last pointers are nullptr.
          if (first_late_scheduled_fiber == nullptr)
            last_late_scheduled_fiber = nullptr;

          // Remove the fiber that is about to execute from the schedule.
          fiber->is_scheduled_to_run_ = false;
          return fiber;
        }
      }

      // There are no messages and there are no fibers. Return to the caller of
      // HandleEverything().
      return fiber_to_return_to_when_out_of_work;
    }
  }
}

// Schedules a fiber to run.
void Scheduler::ScheduleFiber(Fiber* fiber) {
  if (IsPrimaryThread()) {
    if (fiber->is_scheduled_to_run_) return;
    fiber->is_scheduled_to_run_ = true;
    fiber->next_scheduled_fiber_ = nullptr;

    if (primary_last_scheduled_fiber == nullptr) {
      primary_first_scheduled_fiber = fiber;
      primary_last_scheduled_fiber = fiber;
    } else {
      primary_last_scheduled_fiber->next_scheduled_fiber_ = fiber;
      primary_last_scheduled_fiber = fiber;
    }
    return;
  }

  bool wake_up_primary = false;
  {
    SpinlockLock lock(GetSchedulerLock());
    if (fiber->is_scheduled_to_run_)
      // This fiber is already scheduled to run.
      return;
    fiber->is_scheduled_to_run_ = true;
    fiber->next_scheduled_fiber_ = nullptr;

    if (last_scheduled_fiber == nullptr) {
      first_scheduled_fiber = fiber;
      last_scheduled_fiber = fiber;
    } else {
      last_scheduled_fiber->next_scheduled_fiber_ = fiber;
      last_scheduled_fiber = fiber;
    }

    if (!IsPrimaryThread()) wake_up_primary = true;
  }

  if (wake_up_primary) WakeUpPrimaryThread();
}

void Scheduler::ScheduleFiberAfterEvents(Fiber* fiber) {
  if (IsPrimaryThread()) {
    if (fiber->is_scheduled_to_run_) return;
    fiber->is_scheduled_to_run_ = true;
    fiber->next_scheduled_fiber_ = nullptr;

    if (primary_last_late_scheduled_fiber == nullptr) {
      primary_first_late_scheduled_fiber = fiber;
      primary_last_late_scheduled_fiber = fiber;
    } else {
      primary_last_late_scheduled_fiber->next_scheduled_fiber_ = fiber;
      primary_last_late_scheduled_fiber = fiber;
    }
    return;
  }

  bool wake_up_primary = false;
  {
    SpinlockLock lock(GetSchedulerLock());
    if (fiber->is_scheduled_to_run_)
      // This fiber is already scheduled to run.
      return;
    fiber->is_scheduled_to_run_ = true;
    fiber->next_scheduled_fiber_ = nullptr;

    if (last_late_scheduled_fiber == nullptr) {
      first_late_scheduled_fiber = fiber;
      last_late_scheduled_fiber = fiber;
    } else {
      last_late_scheduled_fiber->next_scheduled_fiber_ = fiber;
      last_late_scheduled_fiber = fiber;
    }

    if (!IsPrimaryThread()) wake_up_primary = true;
  }

  if (wake_up_primary) WakeUpPrimaryThread();
}

// Returns a fiber to handle the message, or nullptr if there's
// nothing to do.
Fiber* Scheduler::GetFiberToHandleMessage(ProcessId senders_pid,
                                          const MessageData& message_data) {
  if (message_data.message_id == GetWakeUpMessageId()) return nullptr;

  auto handler = GetMessageHandler(message_data.message_id);
  if (!handler) {
    // Message handler not defined.
    DealWithUnhandledMessage(senders_pid, message_data);
    return nullptr;
  }

  if (handler->fiber_to_wake_up) {
    // There is a sleeping fiber to wake up. Store these values that the fiber
    // will read when it wakes up.
    handler->senders_pid = senders_pid;
    handler->message_data = message_data;
    if (handler->fiber_to_wake_up->IsThread()) {
      handler->fiber_to_wake_up->WakeUp();
      return nullptr;
    }
    return handler->fiber_to_wake_up;
  }

  // Create a fiber to call the handler.
  Fiber* fiber = Fiber::Create(handler, senders_pid, message_data);
  handler.reset();
  return fiber;
}

}  // namespace perception