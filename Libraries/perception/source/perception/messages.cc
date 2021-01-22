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

#include "perception/messages.h"

#include <map>

namespace perception {
namespace {

// The next unique message identifier.
MessageId next_unique_message_id = 0;

// Sleeps until a message. Returns true if a message was received.
bool SleepUntilMessage(ProcessId& senders_pid, MessageId& message_id,
	size_t& param1, size_t& param2, size_t& param3, size_t& param4,
	size_t& param5) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 19;
	volatile register size_t pid_r asm ("r10");
	volatile register size_t message_id_r asm ("rax");
	volatile register size_t param1_r asm ("rsi");
	volatile register size_t param2_r asm ("rdx");
	volatile register size_t param3_r asm ("rbx");
	volatile register size_t param4_r asm ("r8");
	volatile register size_t param5_r asm ("r9");

	__asm__ __volatile__ ("syscall\n":
		"=r"(pid_r), "=r"(message_id_r), "=r"(param1_r), "=r"(param2_r),
		"=r"(param3_r), "=r"(param4_r), "=r"(param5_r): "r" (syscall):
		"rcx", "r11");

	senders_pid = pid_r;
	message_id = message_id_r;
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
	size_t& param1, size_t& param2, size_t& param3, size_t& param4,
	size_t& param5) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 18;
	volatile register size_t pid_r asm ("r10");
	volatile register size_t message_id_r asm ("rax");
	volatile register size_t param1_r asm ("rsi");
	volatile register size_t param2_r asm ("rdx");
	volatile register size_t param3_r asm ("rbx");
	volatile register size_t param4_r asm ("r8");
	volatile register size_t param5_r asm ("r9");

	__asm__ __volatile__ ("syscall\n":
		"=r"(pid_r), "=r"(message_id_r), "=r"(param1_r), "=r"(param2_r),
		"=r"(param3_r), "=r"(param4_r), "=r"(param5_r): "r" (syscall):
		"rcx", "r11");

	senders_pid = pid_r;
	message_id = message_id_r;
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

// A message handler.
struct Handler {
	std::function<void(ProcessId,
		size_t, size_t, size_t, size_t, size_t)> handler_function;
};

// The handler for each message ID.
std::map<MessageId, Handler> handlers_by_message_id;

// Handles an individual message.
void HandleMessage(ProcessId senders_pid, MessageId message_id,
	size_t param1, size_t param2, size_t param3, size_t param4, size_t param5) {
	const auto& handler_itr = handlers_by_message_id.find(message_id);
	if (handler_itr == handlers_by_message_id.end()) {
		// Message handler not defined.
		// TODO: Release any pages that may have been set.
	} else {
		handler_itr->second.handler_function(senders_pid, param1, param2,
			param3, param4, param5);
	}
}

}

// Generates a message identifier that is unique in this instance of the process.
MessageId GenerateUniqueMessageId() {
	return next_unique_message_id++;
}

// Sends a message to a process.
MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2, size_t param3,
	size_t param4, size_t param5) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 17;
	volatile register size_t pid_r asm ("r10") = pid;
	volatile register size_t message_id_r asm ("rax") = message_id;
	volatile register size_t param1_r asm ("rsi") = param1;
	volatile register size_t param2_r asm ("rdx") = param2;
	volatile register size_t param3_r asm ("rbx") = param3;
	volatile register size_t param4_r asm ("r8") = param4;
	volatile register size_t param5_r asm ("r9") = param5;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):
		"r" (syscall), "r"(pid_r), "r"(message_id_r), "r"(param1_r), "r"(param2_r), "r"(param3_r),
		"r"(param4_r), "r"(param5_r):
		"rcx", "r11");
	return (MessageStatus)return_val;
#else
	return MessageStatus::UNSUPPORTED;
#endif
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2, size_t param3,
	size_t param4) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 17;
	volatile register size_t pid_r asm ("r10") = pid;
	volatile register size_t message_id_r asm ("rax") = message_id;
	volatile register size_t param1_r asm ("rsi") = param1;
	volatile register size_t param2_r asm ("rdx") = param2;
	volatile register size_t param3_r asm ("rbx") = param3;
	volatile register size_t param4_r asm ("r8") = param4;
	volatile register size_t param5_r asm ("r9") = 0;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):
		"r" (syscall), "r"(pid_r), "r"(message_id_r), "r"(param1_r), "r"(param2_r), "r"(param3_r),
		"r"(param4_r), "r"(param5_r):
		"rcx", "r11");
	return (MessageStatus)return_val;
#else
	return MessageStatus::UNSUPPORTED;
#endif
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2, size_t param3) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 17;
	volatile register size_t pid_r asm ("r10") = pid;
	volatile register size_t message_id_r asm ("rax") = message_id;
	volatile register size_t param1_r asm ("rsi") = param1;
	volatile register size_t param2_r asm ("rdx") = param2;
	volatile register size_t param3_r asm ("rbx") = param3;
	volatile register size_t param4_r asm ("r8") = 0;
	volatile register size_t param5_r asm ("r9") = 0;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):
		"r" (syscall), "r"(pid_r), "r"(message_id_r), "r"(param1_r), "r"(param2_r), "r"(param3_r),
		"r"(param4_r), "r"(param5_r):
		"rcx", "r11");
	return (MessageStatus)return_val;
#else
	return MessageStatus::UNSUPPORTED;
#endif
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 17;
	volatile register size_t pid_r asm ("r10") = pid;
	volatile register size_t message_id_r asm ("rax") = message_id;
	volatile register size_t param1_r asm ("rsi") = param1;
	volatile register size_t param2_r asm ("rdx") = param2;
	volatile register size_t param3_r asm ("rbx") = 0;
	volatile register size_t param4_r asm ("r8") = 0;
	volatile register size_t param5_r asm ("r9") = 0;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):
		"r" (syscall), "r"(pid_r), "r"(message_id_r), "r"(param1_r), "r"(param2_r), "r"(param3_r),
		"r"(param4_r), "r"(param5_r):
		"rcx", "r11");
	return (MessageStatus)return_val;
#else
	return MessageStatus::UNSUPPORTED;
#endif
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 17;
	volatile register size_t pid_r asm ("r10") = pid;
	volatile register size_t message_id_r asm ("rax") = message_id;
	volatile register size_t param1_r asm ("rsi") = param1;
	volatile register size_t param2_r asm ("rdx") = 0;
	volatile register size_t param3_r asm ("rbx") = 0;
	volatile register size_t param4_r asm ("r8") = 0;
	volatile register size_t param5_r asm ("r9") = 0;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):
		"r" (syscall), "r"(pid_r), "r"(message_id_r), "r"(param1_r), "r"(param2_r), "r"(param3_r),
		"r"(param4_r), "r"(param5_r):
		"rcx", "r11");
	return (MessageStatus)return_val;
#else
	return MessageStatus::UNSUPPORTED;
#endif
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 17;
	volatile register size_t pid_r asm ("r10") = pid;
	volatile register size_t message_id_r asm ("rax") = message_id;
	volatile register size_t param1_r asm ("rsi") = 0;
	volatile register size_t param2_r asm ("rdx") = 0;
	volatile register size_t param3_r asm ("rbx") = 0;
	volatile register size_t param4_r asm ("r8") = 0;
	volatile register size_t param5_r asm ("r9") = 0;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):
		"r" (syscall), "r"(pid_r), "r"(message_id_r), "r"(param1_r), "r"(param2_r), "r"(param3_r),
		"r"(param4_r), "r"(param5_r):
		"rcx", "r11");
	return (MessageStatus)return_val;
#else
	return MessageStatus::UNSUPPORTED;
#endif
}

// Registers the message handler to call when a specific message is received.
void RegisterMessageHandler(MessageId message_id, std::function<void(ProcessId,
	size_t, size_t, size_t, size_t, size_t)> handler) {
	handlers_by_message_id.emplace(std::make_pair(message_id,
		std::move(handler)));
}

// Unregisters the message handler, because we no longer care about handling these messages.
void UnregisterMessageHandler(MessageId message_id) {
	handlers_by_message_id.erase(message_id);
}

// Handles any queued messages, and returns when done. Returns immediately if
// there are no messages queued.
void HandleQueuedMessages() {
	ProcessId pid;
	MessageId message_id;
	size_t param1, param2, param3, param4, param5;
	while (PollForMessage(pid, message_id, param1, param2, param3, param4,
		param5))
		HandleMessage(pid, message_id, param1, param2, param3, param4, param5);
}

// Handles any queued messages, otherwise sleeps until we receive at least one message, and
// then tries to handle it.
void SleepAndHandleQueuedMessage() {
	ProcessId pid;
	MessageId message_id;
	size_t param1, param2, param3, param4, param5;

	if (SleepUntilMessage(pid, message_id, param1, param2, param3, param4,
		param5)) {
		HandleMessage(pid, message_id, param1, param2, param3, param4, param5);
		while (PollForMessage(pid, message_id, param1, param2, param3, param4,
			param5))
			HandleMessage(pid, message_id, param1, param2, param3, param4,
				param5);
	}
}

// Transfers power to the event loop, where the current thread will sleep and dispatch
// messages as they are received.
void TransferToEventLoop() {
	ProcessId pid;
	MessageId message_id;
	size_t param1, param2, param3, param4, param5;

	while (true) {
		if (SleepUntilMessage(pid, message_id, param1, param2, param3, param4,
			param5))
			HandleMessage(pid, message_id, param1, param2, param3, param4,
				param5);
	}
}

}

