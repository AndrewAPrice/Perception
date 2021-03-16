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

#include <iostream>

#include "perception/fibers.h"
#include "perception/messages.h"

namespace perception {
namespace {

// Queue of fibers that are scheduled to run.
// NOTE: Thread local memory isn't always clear.
/*thread_local*/ Fiber* first_scheduled_fiber = nullptr;
/*thread_local*/ Fiber* last_scheduled_fiber = nullptr;

// The fiber to return to when we're out of work instead of sleeping. This is
// the caller of HandleEverything.
/*thread_local*/ Fiber* fiber_to_return_to_when_were_out_of_work = nullptr;
/*thread_local*/ Fiber* fiber_to_return_to_after_sleeping_when_were_out_of_work = nullptr;

// Sleeps until a message. Returns true if a message was received.
bool SleepThreadUntilMessage(ProcessId& senders_pid, MessageId& message_id,
	size_t& metadata, size_t& param1, size_t& param2, size_t& param3,
	size_t& param4, size_t& param5) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 19;
	volatile register size_t pid_r asm ("rbx");
	volatile register size_t message_id_r asm ("rax");
	volatile register size_t metadata_r asm ("rdx");
	volatile register size_t param1_r asm ("rsi");
	volatile register size_t param2_r asm ("r8");
	volatile register size_t param3_r asm ("r9");
	volatile register size_t param4_r asm ("r10");
	volatile register size_t param5_r asm ("r12");

	__asm__ __volatile__ ("syscall\n":
		"=r"(pid_r), "=r"(message_id_r), "=r"(metadata_r), "=r"(param1_r),
		"=r"(param2_r), "=r"(param3_r), "=r"(param4_r), "=r"(param5_r):
		"r" (syscall): "rcx", "r11");

	senders_pid = pid_r;
	message_id = message_id_r;
	metadata = metadata_r;
	param1 = param1_r;
	param2 = param2_r;
	param3 = param3_r;
	param4 = param4_r;
	param5 = param5_r;

	return message_id_r != 0xFFFFFFFFFFFFFFFF;
#else
	return false;
#endif
}

// Polls for a message, returning false immediately if no message was received.
bool PollForMessage(ProcessId& senders_pid, MessageId& message_id,
	size_t& metadata, size_t& param1, size_t& param2, size_t& param3,
	size_t& param4, size_t& param5) {
#if PERCEPTION
	// TODO: Handle metadata
	volatile register size_t syscall asm ("rdi") = 18;
	volatile register size_t pid_r asm ("rbx");
	volatile register size_t message_id_r asm ("rax");
	volatile register size_t metadata_r asm ("rdx");
	volatile register size_t param1_r asm ("rsi");
	volatile register size_t param2_r asm ("r8");
	volatile register size_t param3_r asm ("r9");
	volatile register size_t param4_r asm ("r10");
	volatile register size_t param5_r asm ("r12");

	__asm__ __volatile__ ("syscall\n":
		"=r"(pid_r), "=r"(message_id_r), "=r"(metadata_r), "=r"(param1_r),
		"=r"(param2_r), "=r"(param3_r), "=r"(param4_r), "=r"(param5_r):
		"r" (syscall): "rcx", "r11");

	senders_pid = pid_r;
	message_id = message_id_r;
	metadata = metadata_r;
	param1 = param1_r;
	param2 = param2_r;
	param3 = param3_r;
	param4 = param4_r;
	param5 = param5_r;

	return message_id_r != 0xFFFFFFFFFFFFFFFF;
#else
	return false;
#endif
}

}

// Defers running a function.
void Defer(const std::function<void()>& function) {
	Scheduler::ScheduleFiber(Fiber::Create(function));
}

// Hand over control to the scheduler. This function never returns.
void HandOverControl() {
	if (fiber_to_return_to_when_were_out_of_work != nullptr ||
		fiber_to_return_to_after_sleeping_when_were_out_of_work != nullptr) {
		std::cerr << "We can't nest ::perception::HandOverControl " <<
			"inside of ::perception::FinishAnyPendingWork or " <<
			"WaitForMessagesThenReturn because it will never return." <<
			std::endl;
		 return;
	}

	// Switch to the next scheduled fiber. This will never return.
	Scheduler::GetNextFiberToRun()->JumpTo();
}

// Runs all fibers, handles all events, then returns when there's
// nothing else to do.
void FinishAnyPendingWork() {
	if (fiber_to_return_to_when_were_out_of_work != nullptr ||
		fiber_to_return_to_after_sleeping_when_were_out_of_work != nullptr) {
		std::cerr << "We can't have nested calls" <<
		  "::perception::FinishAnyPendingWork and " << 
		  "WaitForMessagesThenReturn" << std::endl;
		 return;
	}

	// Remember where we are right now.
	fiber_to_return_to_when_were_out_of_work =
		GetCurrentlyExecutingFiber();

	// Switch to the next scheduled fiber.
	Scheduler::GetNextFiberToRun()->SwitchTo();

	// We're back here once there is no more work to do.
	fiber_to_return_to_when_were_out_of_work = nullptr;
}


void WaitForMessagesThenReturn() {
	if (fiber_to_return_to_when_were_out_of_work != nullptr ||
		fiber_to_return_to_after_sleeping_when_were_out_of_work != nullptr) {
		std::cerr << "We can't have nested calls" <<
		  "::perception::FinishAnyPendingWork and " << 
		  "WaitForMessagesThenReturn" << std::endl;
		 return;
	}

	// Remember where we are right now.
	fiber_to_return_to_after_sleeping_when_were_out_of_work =
		GetCurrentlyExecutingFiber();


	// Switch to the next scheduled fiber.
	Scheduler::GetNextFiberToRun()->SwitchTo();

	// We're back here once there is no more work to do.
	fiber_to_return_to_when_were_out_of_work = nullptr;
	fiber_to_return_to_after_sleeping_when_were_out_of_work = nullptr;

}


// Gets the next fiber to run, or sleeps until there is one.
Fiber* Scheduler::GetNextFiberToRun() {

	// Return a fiber if there's one scheduled.
	if (first_scheduled_fiber != nullptr) {
		Fiber* fiber = first_scheduled_fiber;
		first_scheduled_fiber = first_scheduled_fiber->next_scheduled_fiber_;

		// There are no more fibers scheduled, make sure both the first
		// and last pointers are nullptr.
		if (first_scheduled_fiber == nullptr) {
			last_scheduled_fiber = nullptr;
		}

		// We're removing this fiber from the schedule.
		fiber->is_scheduled_to_run_ = false;
		return fiber;
	}

	// Check if there are any messages.
	ProcessId senders_pid;
	MessageId message_id;
	size_t metadata, param1, param2, param3, param4, param5;

	if (fiber_to_return_to_when_were_out_of_work == nullptr) {
		if (fiber_to_return_to_after_sleeping_when_were_out_of_work != nullptr) {
			// We want to sleep first, then return immediately when we run out
			// of work.
			fiber_to_return_to_when_were_out_of_work = 
				fiber_to_return_to_after_sleeping_when_were_out_of_work;
		}

		// Nothing is waiting for to finish, so we'll sleep for the next
		// message.
		while (true) {
			if (!SleepThreadUntilMessage(senders_pid, message_id, metadata, param1, param2,
				param3, param4, param5)) {
				// The thread randomly woke without a message. This shouldn't
				// happen.
				continue;
			}
				
			// Get the fiber to handle this message.
			Fiber* fiber = GetFiberToHandleMessage(senders_pid, message_id, metadata,
				param1, param2, param3, param4, param5);

			// Is there a fiber to handle this message?
			if (fiber != nullptr)
				return fiber;
		}
	} else {
		// Keep looping while there are messages.
		while (PollForMessage(senders_pid, message_id, metadata, param1, param2,
			param3, param4, param5)) {
			// Get the fiber to handle this message.
			Fiber* fiber = GetFiberToHandleMessage(senders_pid, message_id, metadata,
				param1, param2, param3, param4, param5);

			// Is there a fiber to handle this message?
			if (fiber != nullptr)
				return fiber;
		}

		// There are no messages and there are no fibers. We can return
		// to the caller of HandleEverything().
		return fiber_to_return_to_when_were_out_of_work;
	}
}

// Schedules a fiber to run.
void Scheduler::ScheduleFiber(Fiber* fiber) {
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
}
// Returns a fiber to handle the message, or nullptr if there's
// nothing to do.
Fiber* Scheduler::GetFiberToHandleMessage(ProcessId senders_pid, MessageId message_id,
	size_t metadata, size_t param1, size_t param2, size_t param3,
	size_t param4, size_t param5) {

	MessageHandler* handler = GetMessageHandler(message_id);
	if (handler == nullptr) {
		// Message handler not defined.
		DealWithUnhandledMessage(senders_pid, metadata, param1, param4, param5);
		return nullptr;
	}

	// Store these values that the fiber will read when it wakes up.
	handler->senders_pid = senders_pid;
	handler->metadata = metadata;
	handler->param1 = param1;
	handler->param2 = param2;
	handler->param3 = param3;
	handler->param4 = param4;
	handler->param5 = param5;

	if (handler->fiber_to_wake_up) {
		// We have a sleeping fiber to wake up.
		return handler->fiber_to_wake_up;
	}

	// We need to create a fiber to call the handler.
	return Fiber::Create(*handler);
}

}