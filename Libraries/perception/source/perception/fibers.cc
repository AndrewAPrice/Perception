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

#include "perception/fibers.h"

#include <iostream>
#include <memory>

#include "perception/memory.h"
#include "perception/messages.h"
#include "perception/scheduler.h"

namespace perception {
namespace {

constexpr int kNumberOfStackPages = 256;

// The currently executing fiber.
thread_local __attribute__((tls_model("initial-exec")))
Fiber* currently_executing_fiber = nullptr;

// Linked list of unused fibers we can recycle.
thread_local __attribute__((tls_model("initial-exec"))) Fiber* next_free_fiber =
    nullptr;

extern "C" void fiber_single_parameter_entrypoint();

extern "C" void switch_with_fiber(CalleePreservedRegisters* next,
                                  CalleePreservedRegisters* previous);

extern "C" void jump_to_fiber(CalleePreservedRegisters* next);

}  // namespace

// Gets the currently executing fiber.
Fiber* GetCurrentlyExecutingFiber() {
  if (!IsPrimaryThread()) {
    struct ThreadFiberHolder {
      Fiber* fiber = nullptr;
      ~ThreadFiberHolder() {
        if (fiber != nullptr) delete fiber;
      }
    };
    thread_local __attribute__((
        tls_model("initial-exec"))) ThreadFiberHolder thread_fiber;
    if (thread_fiber.fiber == nullptr) {
      thread_fiber.fiber = new Fiber(GetThreadId());
    }
    return thread_fiber.fiber;
  }

  if (currently_executing_fiber == nullptr) {
    currently_executing_fiber = new Fiber(false);
  }

  return currently_executing_fiber;
}

// Sleeps the currently executing fiber or thread.
void Sleep() {
  if (IsPrimaryThread()) {
    Scheduler::GetNextFiberToRun()->SwitchTo();
  } else {
#if defined(PERCEPTION) && !defined(TEST)
    // Syscall 3: Sleep this thread
    volatile register size_t syscall asm("rdi") = 3;
    __asm__ __volatile__("syscall\n" : : "r"(syscall) : "rcx", "r11");
#endif
  }
}

// Initializes the fiber object. You probably want to use Fiber::Create()
// instead.
Fiber::Fiber(bool custom_stack)
    : is_scheduled_to_run_(false), thread_id_(std::nullopt) {
  if (custom_stack) {
    bottom_of_stack_ = (size_t*)AllocateMemoryPages(kNumberOfStackPages);
  } else {
    bottom_of_stack_ = nullptr;
  }
}

Fiber::Fiber(ThreadId thread_id)
    : is_scheduled_to_run_(false),
      thread_id_(thread_id),
      bottom_of_stack_(nullptr) {}

Fiber::~Fiber() {
  if (bottom_of_stack_ != nullptr) {
#if defined(PERCEPTION) && !defined(TEST)
    ReleaseMemoryPages(bottom_of_stack_, kNumberOfStackPages);
#else
    free(bottom_of_stack_);
#endif
  }
}

// Creates a fiber around an entry point.
Fiber* Fiber::Create(const std::function<void()>& function) {
  Fiber* fiber = Create();

  // Keep a copy of the root function and its closures.
  fiber->root_function_ = function;

  // Point to the top of the stack, just under the red-zone.
  size_t* top_of_stack =
      &fiber->bottom_of_stack_[kPageSize * kNumberOfStackPages / 8 - 128 / 8];

  // Write the pointer to the fiber to the stack.
  top_of_stack--;
  *top_of_stack = (size_t)fiber;

  // Write our C++ entrypoint function to the stack.
  top_of_stack--;
  *top_of_stack = (size_t)CallRootFunction;

  // Write out asm entrypoint function to the stack.
  top_of_stack--;
  *top_of_stack = (size_t)fiber_single_parameter_entrypoint;

  // Point the thread's stack pointer to this location.
  fiber->registers_.rsp = (size_t)top_of_stack;

  return fiber;
}

// Creates a fiber to invoke a message handler.
Fiber* Fiber::Create(std::shared_ptr<MessageHandler> message_handler,
                     ProcessId senders_pid, const MessageData& message_data) {
  Fiber* fiber = Create();

  size_t* top_of_stack =
      &fiber->bottom_of_stack_[kPageSize * kNumberOfStackPages / 8];

  // Copy the MessageHandler so it can be recycled.
  top_of_stack -= sizeof(FiberLocalMessageHandler) / 8;
  FiberLocalMessageHandler* local_message_handler =
      new ((FiberLocalMessageHandler*)top_of_stack) FiberLocalMessageHandler();
  local_message_handler->message_handler = message_handler;
  local_message_handler->message_data = message_data;
  local_message_handler->senders_pid = senders_pid;

  // Leave enough room for the safe zone.
  top_of_stack -= 128 / 8;

  // Write the pointer to the message handler to the stack.
  top_of_stack--;
  *top_of_stack = (size_t)local_message_handler;

  // Write our C++ entrypoint function to the stack.
  top_of_stack--;
  *top_of_stack = (size_t)CallMessageHandler;

  // Write out asm entrypoint function to the stack.
  top_of_stack--;
  *top_of_stack = (size_t)fiber_single_parameter_entrypoint;

  // Point the thread's stack pointer to this location.
  fiber->registers_.rsp = (size_t)top_of_stack;

  return fiber;
}

// Returns a Fiber* object, either off the stack or a new one.
Fiber* Fiber::Create() {
  if (next_free_fiber == nullptr) {
    // Creates a new fiber.
    return new Fiber(true);
  } else {
    // Return a fiber off the stack.
    Fiber* fiber = next_free_fiber;
    next_free_fiber = next_free_fiber->next_free_fiber_;
    return fiber;
  }
}

// Switches to this fiber.
void Fiber::SwitchTo() {
  if (!IsPrimaryThread()) {
    std::cerr << "Error: Fiber::SwitchTo called on non-primary thread!"
              << std::endl;
    return;
  }
  Fiber* old_fiber = GetCurrentlyExecutingFiber();

  if (old_fiber == this) return;

  currently_executing_fiber = this;
  switch_with_fiber(&this->registers_, &old_fiber->registers_);
}

// Jumps to this fiber, and forgets about the current context.
void Fiber::JumpTo() {
  if (!IsPrimaryThread()) {
    std::cerr << "Error: Fiber::JumpTo called on non-primary thread!"
              << std::endl;
    return;
  }
  currently_executing_fiber = this;
  jump_to_fiber(&this->registers_);
}

// Wakes up this fiber if it is sleeping.
void Fiber::WakeUp() {
  if (thread_id_.has_value()) {
#if defined(PERCEPTION) && !defined(TEST)
    // Syscall 10: Wake thread
    volatile register size_t syscall asm("rdi") = 10;
    volatile register size_t rax asm("rax") = *thread_id_;
    __asm__ __volatile__("syscall\n" : : "r"(syscall), "r"(rax) : "rcx", "r11");
#endif
  } else {
    Scheduler::ScheduleFiber(this);
  }
}

// Calls the root function of the fiber.
void Fiber::CallRootFunction(Fiber* fiber) {
  if (fiber->root_function_) {
    fiber->root_function_();
  }
  TerminateFiber(fiber);
}

// Calls the message handler for a fiber.
void Fiber::CallMessageHandler(
    FiberLocalMessageHandler* local_message_handler) {
  bool has_handler = false;
  {
    auto message_handler = local_message_handler->message_handler.lock();
    if (message_handler != nullptr) {
      has_handler = true;
      message_handler->handler_function(local_message_handler->senders_pid,
                                        local_message_handler->message_data);
    }
  }

  if (!has_handler) {
    // No longer listening for this message.
  }

  local_message_handler->~FiberLocalMessageHandler();
  TerminateFiber(GetCurrentlyExecutingFiber());
}

// Terminates the fiber after we're done calling the root
// function.
void Fiber::TerminateFiber(Fiber* fiber) {
  Fiber* next_fiber = Scheduler::GetNextFiberToRun();
  // We must release this fiber AFTER getting the next fiber
  // to avoid accidentally overwriting as the last released
  // fiber will be recycled as the next created fiber.
  Release(fiber);
  next_fiber->JumpTo();
}

// Releases a Fiber* that is no longer used.
void Fiber::Release(Fiber* fiber) {
  if (fiber->is_scheduled_to_run_) {
    std::cout << "Something went wrong. Fiber::Release is being called on "
              << "a fiber that is scheduled to run." << std::endl;

    volatile int i = 0;
    std::cout << 4 / i << std::endl;
  }
  if (fiber->bottom_of_stack_ == nullptr) {
    std::cout << "Something went wrong. Fiber::Release is being called on "
              << "the default fiber." << std::endl;

    volatile int i = 0;
    std::cout << 4 / i << std::endl;
  }
  // Release any associated closures.
  fiber->root_function_ = std::function<void()>();

  // Put this fiber on our stack of fibers.
  fiber->next_free_fiber_ = next_free_fiber;
  next_free_fiber = fiber;
}

}  // namespace perception