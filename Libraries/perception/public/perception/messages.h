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

// Sends a message to a process.
MessageStatus SendMessage(size_t pid, size_t message_id, size_t param1, size_t param2, size_t param3,
	size_t param4, size_t param5);
MessageStatus SendMessage(size_t pid, size_t message_id, size_t param1, size_t param2, size_t param3,
	size_t param4);
MessageStatus SendMessage(size_t pid, size_t message_id, size_t param1, size_t param2, size_t param3);
MessageStatus SendMessage(size_t pid, size_t message_id, size_t param1, size_t param2);
MessageStatus SendMessage(size_t pid, size_t message_id, size_t param1);
MessageStatus SendMessage(size_t pid, size_t message_id);

// Sleeps until a message. Returns true if a message was received.
bool SleepUntilMessage(size_t* senders_pid, size_t* message_id, size_t* param1, size_t* param2,
	size_t* param3, size_t* param4, size_t* param5);

// Polls for a message, returning false immediately if no message was received.
bool PollMessage(size_t* senders_pid, size_t* message_id, size_t* param1, size_t* param2,
	size_t* param3, size_t* param4, size_t* param5);
	
}
