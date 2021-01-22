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

#pragma once

#include <functional>

#include "types.h"

namespace perception {

enum class MessageStatus {
	// The message was successfully sent.
	SUCCESS = 0,
	// The process the message was being send to doesn't exist.
	PROCESS_DOESNT_EXIST = 1,
	// The system ran out of memory.
	OUT_OF_MEMORY = 2,
	// The receiving process's queue is full.
	RECEIVERS_QUEUE_IS_FULL = 3,
	// Messaging isn't supported on this platform.
	UNSUPPORTED = 4
};

// Generates a message identifier that is unique in this instance of the process.
MessageId GenerateUniqueMessageId();

// Sends a message to a process.
MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2, size_t param3,
	size_t param4, size_t param5);
MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2, size_t param3,
	size_t param4);
MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2, size_t param3);
MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1, size_t param2);
MessageStatus SendMessage(ProcessId pid, MessageId message_id, size_t param1);
MessageStatus SendMessage(ProcessId pid, MessageId message_id);

// Registers the message handler to call when a specific message is received. Assigning
// another handler to the same Message ID will override that handler.
void RegisterMessageHandler(MessageId message_id, std::function<void(ProcessId,
	size_t, size_t, size_t, size_t, size_t)> handler);

// Unregisters the message handler, because we no longer care about handling these messages.
void UnregisterMessageHandler(MessageId message_id);

// Handles any queued messages, and returns when done. Returns immediately if
// there are no messages queued.
void HandleQueuedMessages();

// Handles any queued messages, otherwise sleeps until we receive at least one message, and
// then tries to handle it.
void SleepAndHandleQueuedMessage();

// Transfers power to the event loop, where the current thread will sleep and dispatch
// messages as they are received.
void TransferToEventLoop();
	
}
