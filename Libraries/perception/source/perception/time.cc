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

#include "perception/time.h"

#include "perception/messages.h"
#include "perception/scheduler.h"

namespace perception {
namespace {

// Tells the kernel to send us a message in a certain number of microseconds
// from now.
void SendMessageInMicrosecondsFromNow(size_t microseconds, size_t message_id) {
#ifdef PERCEPTION
	volatile register size_t syscall asm ("rdi") = 23;
	volatile register size_t microseconds_r asm ("rax") = microseconds;
	volatile register size_t message_id_r asm ("rbx") = message_id;

	__asm__ __volatile__ ("syscall\n"::
		"r" (syscall), "r"(microseconds_r), "r"(message_id_r)
		: "rcx", "r11");
#else
	std::cout << "Implement time.cc:SendMessageAtMicroseconds" << std::endl;
#endif
}

// Tells the kernel to send us a message after a certain number of microseconds
// since the kernel started.
void SendMessageAtMicrosecondsSinceKernelStart(size_t microseconds, size_t message_id) {
#ifdef PERCEPTION
	volatile register size_t syscall asm ("rdi") = 24;
	volatile register size_t microseconds_r asm ("rax") = microseconds;
	volatile register size_t message_id_r asm ("rbx") = message_id;

	__asm__ __volatile__ ("syscall\n"::
		"r" (syscall), "r"(microseconds_r), "r"(message_id_r)
		: "rcx", "r11");
#else
	std::cout << "Implement time.cc:SendMessageAtMicroseconds" << std::endl;
#endif
}

}

// Returns the time since the kernel started.
std::chrono::microseconds GetTimeSinceKernelStarted() {
#ifdef PERCEPTION
	volatile register size_t syscall asm ("rdi") = 25;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):
		"r" (syscall): "rcx", "r11");
	size_t microseconds = return_val;
	return std::chrono::microseconds(microseconds);
#else
	return std::chrono::microseconds(0);
#endif
}

// Sleeps the current fiber and returns after the duration has passed.
void SleepForDuration(std::chrono::microseconds time) {
	MessageId message_id =
		GenerateUniqueMessageId();
	SendMessageInMicrosecondsFromNow(time.count(), message_id);

	ProcessId pid;
	size_t metadata, param1, param2, param3, param4, param5;
	do {
		SleepUntilMessage(message_id,
			pid, metadata, param1, param2, param3, param4,
			param5);

		// Keep sleeping if the message wasn't from the kernel.
	} while (pid != 0);
}

// Sleeps the current fiber and returns after the duration since the
// kernel started has passed.
void SleepUntilTimeSinceKernelStarted(std::chrono::microseconds time) {
	MessageId message_id =
		GenerateUniqueMessageId();
	SendMessageAtMicrosecondsSinceKernelStart(time.count(), message_id);

	ProcessId pid;
	size_t metadata, param1, param2, param3, param4, param5;
	do {
		SleepUntilMessage(message_id,
			pid, metadata, param1, param2, param3, param4,
			param5);

		// Keep sleeping if the message wasn't from the kernel.
	} while (pid != 0);
}

// Calls the on_duration function after a duration has passed.
void AfterDuration(std::chrono::microseconds time,
	std::function<void()> on_duration) {
	MessageId message_id =
		GenerateUniqueMessageId();
	SendMessageInMicrosecondsFromNow(time.count(), message_id);

	::perception::RegisterMessageHandler(message_id,
		[message_id, on_duration] (
		::perception::ProcessId sender,
		size_t, size_t, size_t, size_t, size_t) {
		if (sender != 0)
			// Not from the kernel.
			return;

		::perception::UnregisterMessageHandler(message_id);

		on_duration();
	});
}

// Calls the on_duration function after the duration since the kernel
// started has passed.
void AfterTimeSinceKernelStarted(std::chrono::microseconds time,
	std::function<void()> at_time) {
	MessageId message_id =
		GenerateUniqueMessageId();
	SendMessageAtMicrosecondsSinceKernelStart(time.count(), message_id);

	::perception::RegisterMessageHandler(message_id,
		[message_id, at_time] (
		::perception::ProcessId sender,
		size_t, size_t, size_t, size_t, size_t) {
		if (sender != 0)
			// Not from the kernel.
			return;

		::perception::UnregisterMessageHandler(message_id);

		at_time();
	});
}

}
