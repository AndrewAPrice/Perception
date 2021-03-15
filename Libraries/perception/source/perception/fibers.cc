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

#include "perception/memory.h"
#include "perception/messages.h"
#include "perception/scheduler.h"

#include <iostream>

namespace perception {
namespace {

constexpr int kNumberOfStackPages = 4;

// The currently executing fiber.
/*thread_local*/ Fiber* currently_executing_fiber = nullptr;

// Linked list of unused fibers we can recycle.
/*thread_local*/ Fiber* next_free_fiber = nullptr;

extern "C" void fiber_single_parameter_entrypoint();

extern "C" void switch_with_fiber(CalleePreservedRegisters* next,
	CalleePreservedRegisters* previous);

extern "C" void jump_to_fiber(CalleePreservedRegisters* next);

}

// Gets the currently executing fiber.
Fiber* GetCurrentlyExecutingFiber() {
	if (currently_executing_fiber == nullptr) {
		// The first time GetCurrentlyExecutingFiber is being called.
		// We'll create a new fiber to wrap the currently executing
		// fiber.
		currently_executing_fiber = new Fiber(false);
	}

	return currently_executing_fiber;
}

// Sleeps the currently executing fiber.
void Sleep() {
	Scheduler::GetNextFiberToRun()->SwitchTo();
}

// Initializes the fiber object. You probably want to use Fiber::Create()
// instead.
Fiber::Fiber(bool custom_stack) : 
	is_scheduled_to_run_(false) {
	if (custom_stack) {
		bottom_of_stack_ = (size_t*)AllocateMemoryPages(kNumberOfStackPages);
		//std::cout << "Created fiber with stack: " << std::hex << (size_t)bottom_of_stack_
		//	<< std::dec << std::endl;
	} else {
		bottom_of_stack_ = nullptr;
	}
}

// Creates a fiber around an entry point.
Fiber* Fiber::Create(const std::function<void()>& function) {
	Fiber* fiber = Create();

	// Keep a copy of the root function and its closures.
	fiber->root_function_ = function;

	// Point to the top of the stack, just under the red-zone.
	size_t* top_of_stack = &fiber->bottom_of_stack_[
		kPageSize * kNumberOfStackPages / 8 - 128/8];

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
Fiber* Fiber::Create(const MessageHandler& message_handler) {
	Fiber* fiber = Create();

	// Point to the top of the stack, just under the red-zone.
	size_t* top_of_stack = &fiber->bottom_of_stack_[
		kPageSize * kNumberOfStackPages / 8 - 128/8];

	// Write the pointer to the message handler to the stack.
	top_of_stack--;
	*top_of_stack = (size_t)&message_handler;

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
	Fiber* old_fiber = GetCurrentlyExecutingFiber();

	if (old_fiber == this)
		return;

	currently_executing_fiber = this;
	switch_with_fiber(&this->registers_, &old_fiber->registers_);
}

// Jumps to this fiber, and forgets about the current context.
void Fiber::JumpTo() {
	currently_executing_fiber = this;
	jump_to_fiber(&this->registers_);
}

// Wakes up this fiber if it is sleeping.
void Fiber::WakeUp() {
	Scheduler::ScheduleFiber(this);
}

// Calls the root function of the fiber.
void Fiber::CallRootFunction(Fiber* fiber) {
	fiber->root_function_();
	TerminateFiber(fiber);
}

// Calls the message handler for a fiber.
void Fiber::CallMessageHandler(MessageHandler* message_handler) {
	message_handler->handler_function(message_handler->senders_pid,
		message_handler->metadata, message_handler->param1,
		message_handler->param2, message_handler->param3,
		message_handler->param4, message_handler->param5);
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

}