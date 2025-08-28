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
#include <memory>

#include "linked_list.h"
#include "object_pool.h"
#include "status.h"
#include "types.h"

namespace perception {

class Fiber;

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
  UNSUPPORTED = 4,
  // You attempted to send memory pages, but the address range
  // was invalid.
  INVALID_MEMORY_RANGE = 5
};

struct MessageData {
  MessageId message_id;
  size_t metadata;
  union {
    struct {
      size_t param1, param2, param3, param4, param5;
    };
    uint8 bytes[5 * sizeof(size_t)];
  };
};

// Represents what to do when a message is received.
struct MessageHandler {
  // The fiber to wake up. This is set when a fiber is paused
  // and waiting on a message.
  Fiber* fiber_to_wake_up;

  // The handler function to call. This is only set if
  // fiber_to_wake_up == nullptr.
  std::function<void(ProcessId, const MessageData&)> handler_function;

  // Temporary variables where we store the message data when we
  // create or wake up a fiber.
  ProcessId senders_pid;
  MessageData message_data;
};

struct FiberLocalMessageHandler {
  std::weak_ptr<MessageHandler> message_handler;
  ProcessId senders_pid;
  MessageData message_data;
};

// Were memory pages sent in this message?
bool WereMemoryPagesSentInMessage(size_t metadata);

// Deals with unhandled message, to make sure memory is released and RPCs are
// responded to.
void DealWithUnhandledMessage(ProcessId senders_pid,
                              const MessageData& message_data);

// Generates a message identifier that is unique in this instance of the
// process.
MessageId GenerateUniqueMessageId();

// Converts MessageStatus to Status.
Status ToStatus(MessageStatus status);

// Sends a raw message to a process. Do not use unless you are familiar with
// the Permebuf message protocol, as misuse could lead to memory corruption.
MessageStatus SendRawMessage(ProcessId pid, const MessageData& message_data);

// Sends a message to a process.
MessageStatus SendMessage(ProcessId pid, const MessageData& message_data);

// Registers the message handler to call when a specific message is received.
// Assigning another handler to the same Message ID will override that handler.
// Messages that involve sending over memory pages will not be processed (and
// those pages will be released). TODO: Implement that last bit.
void RegisterMessageHandler(
    MessageId message_id,
    std::function<void(ProcessId, const MessageData&)> callback);

// Registers the message handler to call when a specific message is received.
// Assigning another handler to the same Message ID will override that handler.
// If you don't know what you're doing and don't handle memory pages that are
// sent to you, this can lead to memory leaks.
void RegisterRawMessageHandler(
    MessageId message_id,
    std::function<void(ProcessId, const MessageData&)> callback);

// Unregisters the message handler, because we no longer care about handling
// these messages.
void UnregisterMessageHandler(MessageId message_id);

// Sleeps the current fiber until we receive a message. Waiting for a message
// with a handler assigned to it will override that handler.
void SleepUntilMessage(MessageId message_id, ProcessId& sender,
                       MessageData& data);

// Sleeps the current fiber until we receive a message. Waiting for a message
// with a handler assigned to it will override that handler. If you don't know
// what you're doing and don't handle memory pages that are sent to you, this
// can lead to memory leaks.
void SleepUntilRawMessage(MessageId message_id, ProcessId& sender,
                          MessageData& data);

// Maybe returns a message handler for the given ID, or nullptr.
std::shared_ptr<MessageHandler> GetMessageHandler(MessageId message_id);

}  // namespace perception
