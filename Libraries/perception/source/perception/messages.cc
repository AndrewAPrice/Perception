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

#include "perception/fibers.h"
#include "perception/messages.h"
#include "perception/memory.h"
#include "perception/scheduler.h"

#include <map>

namespace perception {
namespace {

// The next unique message identifier.
MessageId next_unique_message_id = 0;

// The handler for each message ID.
std::map<MessageId, MessageHandler> handlers_by_message_id;

}

// Generates a message identifier that is unique in this instance of the process.
MessageId GenerateUniqueMessageId() {
	return next_unique_message_id++;
}

// Converts MessageStatus to Status.
Status ToStatus(MessageStatus status) {
	switch (status) {
		case MessageStatus::SUCCESS:
			return Status::OK;
	case MessageStatus::PROCESS_DOESNT_EXIST:
		return Status::PROCESS_DOESNT_EXIST;
	case MessageStatus::UNSUPPORTED:
		return Status::UNIMPLEMENTED;
	case MessageStatus::OUT_OF_MEMORY:
	case MessageStatus::RECEIVERS_QUEUE_IS_FULL:
		return Status::OUT_OF_MEMORY;
	case MessageStatus::INVALID_MEMORY_RANGE:
	default:
		return Status::INTERNAL_ERROR;
	}
}

// Were memory pages sent in this message?
bool WereMemoryPagesSentInMessage(size_t metadata) {
	return (metadata & 1) == 1;
}

// Deals with unhandled message, to make sure memory is released and RPCs are
// responded to.
void DealWithUnhandledMessage(ProcessId sender, size_t metadata, size_t param1,
	size_t param4, size_t param5) {
	if (WereMemoryPagesSentInMessage(metadata)) {
		// Release the memory that was sent to us.
		ReleaseMemoryPages((void*)param4, param5);
	}

	if (((metadata >> 1) & 0b11) != 0) {
		// This is an RPC that expects a response. We need to respond
		// to tell them this service or channel doesn't exist.
		SendMessage(sender,
			/*message_id=*/(::perception::MessageId)param1,
			/*param_1=*/(size_t)Status::SERVICE_DOESNT_EXIST);
	}
}

// Sends a message to a process.
MessageStatus SendRawMessage(ProcessId pid, MessageId message_id, size_t metadata,
	size_t param1, size_t param2, size_t param3, size_t param4, size_t param5) {
#if PERCEPTION
	volatile register size_t syscall asm ("rdi") = 17;
	volatile register size_t pid_r asm ("rbx") = pid;
	volatile register size_t message_id_r asm ("rax") = message_id;
	volatile register size_t metadata_r asm ("rdx") = metadata;
	volatile register size_t param1_r asm ("rsi") = param1;
	volatile register size_t param2_r asm ("r8") = param2;
	volatile register size_t param3_r asm ("r9") = param3;
	volatile register size_t param4_r asm ("r10") = param4;
	volatile register size_t param5_r asm ("r12") = param5;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):
		"r" (syscall), "r"(pid_r), "r"(message_id_r), "r"(metadata_r),
		"r"(param1_r), "r"(param2_r), "r"(param3_r), "r"(param4_r),
		"r"(param5_r): "rcx", "r11");
	return (MessageStatus)return_val;
#else
	return MessageStatus::UNSUPPORTED;
#endif
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2, size_t param3,
	size_t param4, size_t param5) {
	return SendRawMessage(pid, message_id, 0, param1, param2, param3, param4, param5);
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2, size_t param3,
	size_t param4) {
	return SendRawMessage(pid, message_id, 0, param1, param2, param3, param4, 0);
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2, size_t param3) {
	return SendRawMessage(pid, message_id, 0, param1, param2, param3, 0, 0);
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2) {
	return SendRawMessage(pid, message_id, 0, param1, param2, 0, 0, 0);
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1) {
	return SendRawMessage(pid, message_id, 0, param1, 0, 0, 0, 0);
}

MessageStatus SendMessage(ProcessId pid, MessageId message_id) {
	return SendRawMessage(pid, message_id, 0, 0, 0, 0, 0, 0);
}

// Registers the message handler to call when a specific message is received.
void RegisterMessageHandler(MessageId message_id, std::function<void(ProcessId,
	size_t, size_t, size_t, size_t, size_t)> callback) {
	RegisterRawMessageHandler(message_id, [callback = std::move(callback)](
		ProcessId sender, size_t metadata, size_t param1, size_t param2,
		size_t param3, size_t param4, size_t param5) {
		if (metadata != 0) {
			// This is an RPC, and not something a basic message handler should
			// deal with.
			DealWithUnhandledMessage(sender, metadata, param1, param4, param5);
			return;
		}
		callback(sender, param1, param2, param3, param4, param5);
	});
}

// Registers the message handler to call when a specific message is received. Assigning
// another handler to the same Message ID will override that handler. If you don't know
// what you're doing and don't handle memory pages that are sent to you, this can lead
// to memory leaks.
void RegisterRawMessageHandler(MessageId message_id, std::function<void(ProcessId,
	size_t, size_t, size_t, size_t, size_t, size_t)> callback) {
	// Erase already existing message handler.
	auto handlers_by_message_id_itr = handlers_by_message_id.find(message_id);
	if (handlers_by_message_id_itr != handlers_by_message_id.end())
		handlers_by_message_id.erase(handlers_by_message_id_itr);

	MessageHandler handler;
	handler.fiber_to_wake_up = nullptr;
	handler.handler_function = std::move(callback);
	handlers_by_message_id.emplace(std::make_pair(message_id,
		std::move(handler)));
}

// Unregisters the message handler, because we no longer care about handling these messages.
void UnregisterMessageHandler(MessageId message_id) {
	auto handlers_by_message_id_itr = handlers_by_message_id.find(message_id);
	if (handlers_by_message_id_itr != handlers_by_message_id.end())
		handlers_by_message_id.erase(message_id);
}

// Sleeps the current fiber until we receive a message. Waiting for a message
// with a handler assigned to it will override that handler.
void SleepUntilMessage(MessageId message_id, ProcessId& sender,
	size_t& metadata, size_t& param1, size_t& param2, size_t& param3,
	size_t& param4, size_t& param5) {
	SleepUntilRawMessage(message_id, sender, metadata, param1, param2, param3,
		param4, param5);
	if (metadata != 0) {
		// This is an RPC, and not something a basic message handler should
		// deal with.
		DealWithUnhandledMessage(sender, metadata, param1, param4, param5);
		sender = 0;
		metadata = 0;
		param1 = 0;
		param2 = 0;
		param3 = 0;
		param4 = 0;
		param5 = 0;
	}
}

// Sleeps the current fiber until we receive a message. Waiting for a message
// with a handler assigned to it will override that handler. If you don't know
// what you're doing and don't handle memory pages that are sent to you, this
// can lead to memory leaks.
void SleepUntilRawMessage(MessageId message_id, ProcessId& sender,
	size_t& metadata, size_t& param1, size_t& param2, size_t& param3,
	size_t& param4, size_t& param5) {
	// Register the handler to wake us up.
	MessageHandler handler;
	handler.fiber_to_wake_up = GetCurrentlyExecutingFiber();
	handlers_by_message_id.emplace(std::make_pair(message_id,
		std::move(handler)));

	// Yield this fiber.
	Sleep();

	// Get our handler.
	auto handler_itr = handlers_by_message_id.find(message_id);
	if (handler_itr == handlers_by_message_id.end()) {
		// This should never happen, but we'll have to return something.
		sender = 0;
		metadata = 0;
		param1 = 0;
		param2 = 0;
		param3 = 0;
		param4 = 0;
		param5 = 0;
		return;
	}

	sender = handler_itr->second.senders_pid;
	metadata = handler_itr->second.metadata;
	param1 = handler_itr->second.param1;
	param2 = handler_itr->second.param2;
	param3 = handler_itr->second.param3;
	param4 = handler_itr->second.param4;
	param5 = handler_itr->second.param5;

	// We can stop listening now.
	handlers_by_message_id.erase(handler_itr);
}
// Maybe returns a message handler for the given ID, or nullptr.
MessageHandler* GetMessageHandler(MessageId message_id) {
	auto handler_itr = handlers_by_message_id.find(message_id);
	if (handler_itr == handlers_by_message_id.end())
		return nullptr;
	else
		return &handler_itr->second;
}

}

