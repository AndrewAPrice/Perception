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

#include "perception/services.h"

#include "perception/messages.h"

namespace perception {
namespace {
constexpr int kMaxServiceNameLength = 80;
}

#ifdef debug_BUILD_
// We can't pass RBP in and out of inline assembly in debug builds, so we'll
// use these variables to temporarily store and overwrite RBP.
extern "C" {
	size_t _perception_services__temp_rbp;
	size_t _perception_services__syscall_rbp;
}
#endif

void RegisterService(perception::MessageId message_id, std::string_view name) {
#ifdef PERCEPTION
	if (name.size() > kMaxServiceNameLength)
		return;

	size_t service_name_words[kMaxServiceNameLength / 8];
	memset(service_name_words, 0, kMaxServiceNameLength);
	memcpy(service_name_words, &name[0], name.size());

	volatile register size_t syscall asm ("rdi") = 32;
#ifdef debug_BUILD_
	// TODO: Mutex for debug builds.
	_perception_services__syscall_rbp = (size_t)message_id;
#else
	volatile register size_t message_id_r asm ("rbp") = (size_t)message_id;
#endif
	volatile register size_t name_1 asm ("rax") = service_name_words[0];
	volatile register size_t name_2 asm ("rbx") = service_name_words[1];
	volatile register size_t name_3 asm ("rdx") = service_name_words[2];
	volatile register size_t name_4 asm ("rsi") = service_name_words[3];
	volatile register size_t name_5 asm ("r8") = service_name_words[4];
	volatile register size_t name_6 asm ("r9") = service_name_words[5];
	volatile register size_t name_7 asm ("r10") = service_name_words[6];
	volatile register size_t name_8 asm ("r12") = service_name_words[7];
	volatile register size_t name_9 asm ("r13") = service_name_words[8];
	volatile register size_t name_10 asm ("r14") = service_name_words[9];

#ifdef debug_BUILD_
	__asm__ __volatile__ (R"(
		mov %%rbp, _perception_services__temp_rbp
		mov _perception_services__syscall_rbp, %%rbp
		syscall
		mov _perception_services__temp_rbp, %%rbp
	)"::
	"r"(syscall), "r"(name_1), "r"(name_2), "r"(name_3), "r"(name_4),
	"r"(name_5), "r"(name_6), "r"(name_7), "r"(name_8), "r"(name_9),
	"r"(name_10):
		"rcx", "r11", "memory");
#else
	__asm__ __volatile__ ("syscall\n"::
	"r"(syscall), "r"(message_id_r), "r"(name_1), "r"(name_2), "r"(name_3), "r"(name_4),
	"r"(name_5), "r"(name_6), "r"(name_7), "r"(name_8), "r"(name_9),
	"r"(name_10):
		"rcx", "r11");
#endif
#endif
}

void UnregisterService(perception::MessageId message_id) {
#ifdef PERCEPTION
	volatile register size_t syscall asm ("rdi") = 33;
	volatile register size_t message_id_r asm ("rax") = (size_t)message_id;

	__asm__ __volatile__ ("syscall\n"::
	"r"(syscall), "r"(message_id_r):
		"rcx", "r11");
#endif
}

// Finds the first service with a given name. Returns if there is at least one
// service.
bool FindFirstInstanceOfService(std::string_view name, ProcessId& process,
	MessageId& message) {
#ifdef PERCEPTION
	if (name.size() >= kMaxServiceNameLength)
		return false;

	size_t service_name_words[kMaxServiceNameLength / 8];
	memset(service_name_words, 0, kMaxServiceNameLength);
	memcpy(service_name_words, &name[0], name.size());

	volatile register size_t syscall asm ("rdi") = 34;
#ifdef debug_BUILD_
	// TODO: Mutex for debug builds.
	_perception_services__syscall_rbp = 0;
#else
	volatile register size_t min_pid asm ("rbp") = 0;
#endif
	volatile register size_t min_message_id asm ("rax") = 0;
	volatile register size_t name_1 asm ("rbx") = service_name_words[0];
	volatile register size_t name_2 asm ("rdx") = service_name_words[1];
	volatile register size_t name_3 asm ("rsi") = service_name_words[2];
	volatile register size_t name_4 asm ("r8") = service_name_words[3];
	volatile register size_t name_5 asm ("r9") = service_name_words[4];
	volatile register size_t name_6 asm ("r10") = service_name_words[5];
	volatile register size_t name_7 asm ("r12") = service_name_words[6];
	volatile register size_t name_8 asm ("r13") = service_name_words[7];
	volatile register size_t name_9 asm ("r14") = service_name_words[8];
	volatile register size_t name_10 asm ("r15") = service_name_words[9];

	// We only care about the first service.
	volatile register size_t number_of_services asm ("rdi");
#ifndef debug_BUILD_
	volatile register size_t pid_1 asm ("rbp");
#endif
	volatile register size_t message_id_1 asm ("rax");

#ifdef debug_BUILD_
	__asm__ __volatile__ (R"(
		mov %%rbp, _perception_services__temp_rbp
		mov _perception_services__syscall_rbp, %%rbp
		syscall
		mov %%rbp, _perception_services__syscall_rbp
		mov _perception_services__temp_rbp, %%rbp
	)":
		"=r"(number_of_services), "=r"(message_id_1) :
		"r"(syscall), "r"(min_message_id), "r"(name_1), "r"(name_2),
		"r"(name_3), "r"(name_4), "r"(name_5), "r"(name_6), "r"(name_7),
		"r"(name_8), "r"(name_9), "r"(name_10):
		"rcx", "r11", "memory");
	size_t pid_1 = _perception_services__syscall_rbp;
#else
	__asm__ __volatile__ ("syscall\n":
		"=r"(number_of_services), "=r"(message_id_1), "=r"(pid_1) :
		"r"(syscall), "r"(min_pid), "r"(min_message_id), "r"(name_1), "r"(name_2),
		"r"(name_3), "r"(name_4), "r"(name_5), "r"(name_6), "r"(name_7),
		"r"(name_8), "r"(name_9), "r"(name_10):
		"rcx", "r11");
#endif

	if (number_of_services < 1) {
		return false;
	}
	process = pid_1;
	message = message_id_1;
	return true;
#else
	return false;
#endif
}

// Calls the handler for each instance of the service.
void ForEachInstanceOfService(std::string_view name,
	const std::function<void(ProcessId, MessageId)>& on_each_service) {
#ifdef PERCEPTION
	if (name.size() >= kMaxServiceNameLength)
		return;

	size_t service_name_words[kMaxServiceNameLength / 8];
	memset(service_name_words, 0, kMaxServiceNameLength);
	memcpy(service_name_words, &name[0], name.size());

	size_t starting_pid = 0;
	size_t starting_message_id = 0;

	// Keep looping over all of the pages until we have a result.
	while (true) {
		// Fetch this page of results.
		volatile register size_t syscall asm ("rdi") = 34;
#ifdef debug_BUILD_
		// TODO: Mutex for debug builds.
		_perception_services__syscall_rbp = (size_t)starting_pid;
#else
		volatile register size_t min_pid asm ("rbp") = starting_pid;
#endif
		volatile register size_t min_message_id asm ("rax") = starting_message_id;
		volatile register size_t name_1 asm ("rbx") = service_name_words[0];
		volatile register size_t name_2 asm ("rdx") = service_name_words[1];
		volatile register size_t name_3 asm ("rsi") = service_name_words[2];
		volatile register size_t name_4 asm ("r8") = service_name_words[3];
		volatile register size_t name_5 asm ("r9") = service_name_words[4];
		volatile register size_t name_6 asm ("r10") = service_name_words[5];
		volatile register size_t name_7 asm ("r12") = service_name_words[6];
		volatile register size_t name_8 asm ("r13") = service_name_words[7];
		volatile register size_t name_9 asm ("r14") = service_name_words[8];
		volatile register size_t name_10 asm ("r15") = service_name_words[9];

		volatile register size_t number_of_services_r asm ("rdi");
#ifndef debug_BUILD_
		volatile register size_t pid_1_r asm ("rbp");
#endif
		volatile register size_t message_id_1_r asm ("rax");
		volatile register size_t pid_2_r asm ("rbx");
		volatile register size_t message_id_2_r asm ("rdx");
		volatile register size_t pid_3_r asm ("rsi");
		volatile register size_t message_id_3_r asm ("r8");
		volatile register size_t pid_4_r asm ("r9");
		volatile register size_t message_id_4_r asm ("r10");
		volatile register size_t pid_5_r asm ("r12");
		volatile register size_t message_id_5_r asm ("r13");
		volatile register size_t pid_6_r asm ("r14");
		volatile register size_t message_id_6_r asm ("r15");

#ifdef debug_BUILD_
		__asm__ __volatile__ (R"(
			mov %%rbp, _perception_services__temp_rbp
			mov _perception_services__syscall_rbp, %%rbp
			syscall
			mov %%rbp, _perception_services__syscall_rbp
			mov _perception_services__temp_rbp, %%rbp
		)":"=r"(number_of_services_r),
			"=r"(message_id_1_r),"=r"(pid_2_r),"=r"(message_id_2_r),"=r"(pid_3_r),
			"=r"(message_id_3_r),"=r"(pid_4_r),"=r"(message_id_4_r),"=r"(pid_5_r),
			"=r"(message_id_5_r),"=r"(pid_6_r),"=r"(message_id_6_r):
		"r"(syscall), "r"(min_message_id), "r"(name_1), "r"(name_2),
		"r"(name_3), "r"(name_4), "r"(name_5), "r"(name_6), "r"(name_7), "r"(name_8),
		"r"(name_9), "r"(name_10), "r"(name_10):
			"rcx", "r11", "memory");
		size_t pid_1_r = _perception_services__syscall_rbp;
#else
		__asm__ __volatile__ ("syscall\n":"=r"(number_of_services_r),"=r"(pid_1_r),
			"=r"(message_id_1_r),"=r"(pid_2_r),"=r"(message_id_2_r),"=r"(pid_3_r),
			"=r"(message_id_3_r),"=r"(pid_4_r),"=r"(message_id_4_r),"=r"(pid_5_r),
			"=r"(message_id_5_r),"=r"(pid_6_r),"=r"(message_id_6_r):
		"r"(syscall), "r"(min_pid), "r"(min_message_id), "r"(name_1), "r"(name_2),
		"r"(name_3), "r"(name_4), "r"(name_5), "r"(name_6), "r"(name_7), "r"(name_8),
		"r"(name_9), "r"(name_10), "r"(name_10): "rcx", "r11");
#endif

		size_t number_of_services = number_of_services_r;
		size_t pid_1 = pid_1_r;
		size_t message_id_1 = message_id_1_r;
		size_t pid_2 = pid_2_r;
		size_t message_id_2 = message_id_2_r;
		size_t pid_3 = pid_3_r;
		size_t message_id_3 = message_id_3_r;
		size_t pid_4 = pid_4_r;
		size_t message_id_4 = message_id_4_r;
		size_t pid_5 = pid_5_r;
		size_t message_id_5 = message_id_5_r;
		size_t pid_6 = pid_6_r;
		size_t message_id_6 = message_id_6_r;

		// Call backs.
		if (number_of_services >= 1)
			on_each_service(pid_1, message_id_1);
		if (number_of_services >= 2)
			on_each_service(pid_2, message_id_2);
		if (number_of_services >= 3)
			on_each_service(pid_3, message_id_3);
		if (number_of_services >= 4)
			on_each_service(pid_4, message_id_4);
		if (number_of_services >= 5)
			on_each_service(pid_5, message_id_5);
		if (number_of_services >= 6)
			on_each_service(pid_6, message_id_6);

		// We don't have another page of results.
		if (number_of_services <= 6)
			return;

		// Start the next page just afer the last process ID of this page.
		starting_pid = pid_6;
		starting_message_id = message_id_6 + 1;
	}
#endif
}

// Calls the handler for each instance of the service that currently exists,
// and every time a new instance is registered.
MessageId NotifyOnEachNewServiceInstance(std::string_view name,
	const std::function<void(ProcessId, MessageId)>& on_each_service) {
#ifdef PERCEPTION
	if (name.size() > kMaxServiceNameLength)
		return 0;

	MessageId message_id = GenerateUniqueMessageId();
	RegisterMessageHandler(message_id, [on_each_service](
		ProcessId sender, size_t param_1, size_t param_2, size_t, size_t,
		size_t) {
		// We only believe the kernel.
		if (sender != 0) return;
		on_each_service((ProcessId)param_1, (MessageId)param_2);
	});

	size_t service_name_words[kMaxServiceNameLength / 8];
	memset(service_name_words, 0, kMaxServiceNameLength);
	memcpy(service_name_words, &name[0], name.size());

	volatile register size_t syscall asm ("rdi") = 35;
#ifdef debug_BUILD_
	// TODO: Mutex for debug builds.
	_perception_services__syscall_rbp = message_id;
#else
	volatile register size_t message_id_r asm ("rbp") = message_id;
#endif
	volatile register size_t name_1 asm ("rax") = service_name_words[0];
	volatile register size_t name_2 asm ("rbx") = service_name_words[1];
	volatile register size_t name_3 asm ("rdx") = service_name_words[2];
	volatile register size_t name_4 asm ("rsi") = service_name_words[3];
	volatile register size_t name_5 asm ("r8") = service_name_words[4];
	volatile register size_t name_6 asm ("r9") = service_name_words[5];
	volatile register size_t name_7 asm ("r10") = service_name_words[6];
	volatile register size_t name_8 asm ("r12") = service_name_words[7];
	volatile register size_t name_9 asm ("r13") = service_name_words[8];
	volatile register size_t name_10 asm ("r14") = service_name_words[9];

#ifdef debug_BUILD_
	__asm__ __volatile__ (R"(
		mov %%rbp, _perception_services__temp_rbp
		mov _perception_services__syscall_rbp, %%rbp
		syscall
		mov _perception_services__temp_rbp, %%rbp
	)"::
		"r"(syscall), "r"(name_1), "r"(name_2), "r"(name_3), "r"(name_4),
		"r"(name_5), "r"(name_6), "r"(name_7), "r"(name_8), "r"(name_9),
		"r"(name_10) : "rcx", "r11", "memory");
#else
	__asm__ __volatile__ ("syscall\n"::
		"r"(syscall), "r"(message_id_r), "r"(name_1), "r"(name_2), "r"(name_3), "r"(name_4),
		"r"(name_5), "r"(name_6), "r"(name_7), "r"(name_8), "r"(name_9),
		"r"(name_10) : "rcx", "r11");
#endif

	return message_id;
#else
	return 0;
#endif

}

// Stops calling the handler passed to NotifyOnEachService each time a new
// instance of the service is registered.
void StopNotifyingOnEachNewServiceInstance(MessageId message_id) {
#ifdef PERCEPTION
	UnregisterMessageHandler(message_id);
	volatile register size_t syscall_num asm ("rdi") = 36;
	volatile register size_t message_id_r asm ("rax") = message_id;

	__asm__ __volatile__ ("syscall\n"::"r"(syscall_num),
		"r"(message_id_r): "rcx", "r11");
#endif
}

// Calls the handler when the service disappears. The handler automatically
// unregisters if it gets triggered (although it's safe to accidentally
// call StopNotifyWhenServiceDisappears if it has triggered.)
MessageId NotifyWhenServiceDisappears(ProcessId process_id,
	MessageId message_id, const std::function<void()>& on_disappearance) {
#ifdef PERCEPTION
	MessageId notification_message_id = GenerateUniqueMessageId();
	RegisterMessageHandler(notification_message_id,
		[notification_message_id, on_disappearance](
		ProcessId sender, size_t, size_t, size_t, size_t,
		size_t) {
		// We only believe the kernel.
		if (sender != 0) return;
		on_disappearance();
		UnregisterMessageHandler(notification_message_id);
	});

	volatile register size_t syscall_num asm ("rdi") = 37;
	volatile register size_t process_id_r asm ("rax") = (size_t)process_id;
	volatile register size_t message_id_r asm ("rbx") = (size_t)message_id;
	volatile register size_t notification_message_id_r asm ("rdx") = message_id;

	__asm__ __volatile__ ("syscall\n"::"r"(syscall_num), "r"(process_id_r),
		"r"(message_id_r), "r"(notification_message_id_r): "rcx", "r11");
	return notification_message_id;
#else
	return 0;
#endif
}

// No longer calls the handler when the service disappears.
void StopNotifyWhenServiceDisappears(MessageId message_id) {
#ifdef PERCEPTION
	UnregisterMessageHandler(message_id);
	volatile register size_t syscall_num asm ("rdi") = 38;
	volatile register size_t message_id_r asm ("rax") = message_id;

	__asm__ __volatile__ ("syscall\n"::"r"(syscall_num),
		"r"(message_id_r): "rcx", "r11");
#endif
}

}
