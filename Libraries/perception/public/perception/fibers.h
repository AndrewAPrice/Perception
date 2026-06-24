// Copyright 2021 Google LLC
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

#pragma once

#include <functional>
#include <optional>

#include "perception/messages.h"
#include "perception/threads.h"
#include "types.h"

namespace perception {

class Fiber;
class Scheduler;

// Registers that need to be preserved between switching fibers.
struct CalleePreservedRegisters {
  size_t r15, r14, r13, r12, rbx, rbp, rsp;
};

// Gets the currently executing fiber.
Fiber* GetCurrentlyExecutingFiber();

// Puts the currently executing fiber to sleep. If this is called on the
// primary thread, the execution continues to run on the next queued fiber
// before the thread truely sleeps. If this is called on any other thread, it
// really puts the thread to sleep.
void Sleep();

// Represents a fiber - which is a lightweight unit of execution that runs on
// the primary thread, can be queued up, and is cooperatively multitasked when
// blocked. The Fiber class will also wrap true threads (if IsThread() returns
// true), which will run in parallel to the primary thread.
class Fiber {
 public:
  // Initializes the fiber object. You probably want to use Fiber::Create()
  // instead.
  Fiber(bool custom_stack);

  // Initializes a thread-wrapping fiber object.
  Fiber(ThreadId thread_id);

  // Destructor.
  ~Fiber();

  // Expose whether this represents a thread or a real fiber.
  bool IsThread() { return thread_id_.has_value(); }

  // Creates a fiber around an entry point.
  static Fiber* Create(const std::function<void()>& function);
  static Fiber* Create(std::function<void()>&& function);

  // Creates a fiber to invoke a message handler.
  static Fiber* Create(const std::shared_ptr<MessageHandler>& message_handler,
                       ProcessId senders_pid, const MessageData& message_data);

  // Switches to this fiber. This must only be called from the primary thread
  // on true fibers. Typically handled by the scheduler and not to be manually
  // invoked.
  void SwitchTo();

  // Jumps to this fiber. This must only be called from the primary thread
  // on true fibers. Typically handled by the scheduler and not to be manually
  // invoked.
  void JumpTo();

  // Wakes up this fiber if it is sleeping. If this is a fiber, it gets queued
  // to run on the primary thread. If this is a thread, it is woken up.
  void WakeUp();

 private:
  friend Scheduler;

  // Returns a Fiber* object, either off the stack or a new one.
  static Fiber* Create();

  // Prepares the fiber's stack lazily before execution.
  void PrepareStack();

  // The state of the registers when we context switch.
  CalleePreservedRegisters registers_;

  // Bottom of the fiber's stack.
  size_t* bottom_of_stack_;

  // The root function to run.
  std::function<void()> root_function_;

  // Message handler data if this fiber was created to handle a message.
  std::weak_ptr<MessageHandler> message_handler_;
  ProcessId senders_pid_;
  MessageData message_data_;

  // Is this fiber scheduled to run?
  bool is_scheduled_to_run_;
  bool is_custom_fiber_;

  // We keep a stack of free fibers.
  union {
    Fiber* next_free_fiber_;
    Fiber* next_scheduled_fiber_;
  };

  // Calls the root function of the fiber.
  static void CallRootFunction(Fiber* fiber);

  // Calls the message handler for a fiber.
  static void CallMessageHandler(Fiber* fiber);

  // Terminates the fiber after we're done calling the root
  // function.
  static void TerminateFiber(Fiber* fiber);

  // Releases a Fiber* that is no longer used.
  static void Release(Fiber* fiber);

  std::optional<ThreadId> thread_id_;
};

}  // namespace perception
