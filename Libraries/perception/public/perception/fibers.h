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

#include "types.h"

namespace perception {

class Fiber;
struct MessageHandler;
class Scheduler;

// Registers that need to be preserved between switching fibers.	
struct CalleePreservedRegisters {
	size_t r15, r14, r13, r12, rbx, rbp, rsp;
};

// Gets the currently executing fiber.
Fiber* GetCurrentlyExecutingFiber();

// Sleeps the currently executing fiber.
void Sleep();

class Fiber {
public:
	// Initializes the fiber object. You probably want to use Fiber::Create()
	// instead.
	Fiber(bool custom_stack);

	// Creates a fiber around an entry point.
	static Fiber* Create(const std::function<void()>& function);

	// Creates a fiber to invoke a message handler.
	static Fiber* Create(const MessageHandler& message_handler);

	// Returns a Fiber* object, either off the stack or a new one.
	static Fiber* Create();

	// Switches to this fiber.
	void SwitchTo();

	// Jumps to this fiber - you probably don't want to call this.
	void JumpTo();

	// Wakes up this fiber if it is sleeping.
	void WakeUp();

private:
	friend Scheduler;

	// The state of the registers when we context switch.
	CalleePreservedRegisters registers_;

	// Bottom of the fiber's stack.
	size_t* bottom_of_stack_;

	// The root function to run.
	std::function<void()> root_function_;

	// Is this fiber scheduled to run?
	bool is_scheduled_to_run_;

	// We keep a stack of free fibers.
	union {
		Fiber* next_free_fiber_;
		Fiber* next_scheduled_fiber_;
	};

	// Calls the root function of the fiber.
	static void CallRootFunction(Fiber* fiber);

	// Calls the message handler for a fiber.
	static void CallMessageHandler(MessageHandler* message_handler);

	// Terminates the fiber after we're done calling the root
	// function.
	static void TerminateFiber(Fiber* fiber);

	// Releases a Fiber* that is no longer used.
	static void Release(Fiber* fiber);
};

}
