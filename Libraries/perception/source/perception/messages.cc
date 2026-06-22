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

#include <atomic>
#include <map>

#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/scheduler.h"

namespace perception {
namespace {

struct Spinlock {
  std::atomic_flag flag = ATOMIC_FLAG_INIT;
  void Lock() {
    while (flag.test_and_set(std::memory_order_acquire)) {
    }
  }
  void Unlock() { flag.clear(std::memory_order_release); }
};

struct SpinlockLock {
  Spinlock& lock_;
  SpinlockLock(Spinlock& lock) : lock_(lock) { lock_.Lock(); }
  ~SpinlockLock() { lock_.Unlock(); }
};

Spinlock& GetMessagesLock() {
  static Spinlock lock;
  return lock;
}

// The next unique message identifier.
MessageId next_unique_message_id = 0;

// The handler for each message ID.
std::map<MessageId, std::shared_ptr<MessageHandler>>* handlers_by_message_id_ptr = nullptr;

std::map<MessageId, std::shared_ptr<MessageHandler>>& GetHandlersByMessageId() {
  if (handlers_by_message_id_ptr == nullptr) {
    handlers_by_message_id_ptr = new std::map<MessageId, std::shared_ptr<MessageHandler>>();
  }
  return *handlers_by_message_id_ptr;
}

}  // namespace

// Generates a message identifier that is unique in this instance of the
// process.
MessageId GenerateUniqueMessageId() {
  SpinlockLock lock(GetMessagesLock());
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
void DealWithUnhandledMessage(ProcessId sender,
                              const MessageData& message_data) {
  if (WereMemoryPagesSentInMessage(message_data.metadata)) {
    // Release the memory that was sent.
    ReleaseMemoryPages((void*)message_data.param4, message_data.param5);
  }

  if (((message_data.metadata >> 1) & 0b11) != 0) {
    // This is an RPC that expects a response. Respond to indicate that the
    // service or channel doesn't exist.
    MessageData response_data = {};
    response_data.message_id = message_data.param2;
    response_data.metadata = 0;
    response_data.param1 = (size_t)Status::SERVICE_DOESNT_EXIST;

    SendMessage(sender, response_data);
  }
}

// Sends a message to a process.
MessageStatus SendRawMessage(ProcessId pid, const MessageData& message_data) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 17;
  volatile register size_t pid_r asm("rbx") = pid;
  volatile register size_t message_id_r asm("rax") = message_data.message_id;
  volatile register size_t metadata_r asm("rdx") = message_data.metadata;
  volatile register size_t param1_r asm("rsi") = message_data.param1;
  volatile register size_t param2_r asm("r8") = message_data.param2;
  volatile register size_t param3_r asm("r9") = message_data.param3;
  volatile register size_t param4_r asm("r10") = message_data.param4;
  volatile register size_t param5_r asm("r12") = message_data.param5;
  volatile register size_t return_val asm("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val)
                       : "r"(syscall), "r"(pid_r), "r"(message_id_r),
                         "r"(metadata_r), "r"(param1_r), "r"(param2_r),
                         "r"(param3_r), "r"(param4_r), "r"(param5_r)
                       : "rcx", "r11");
  return (MessageStatus)return_val;
#else
  return MessageStatus::UNSUPPORTED;
#endif
}

MessageStatus SendMessage(ProcessId pid, const MessageData& message_data) {
  return SendRawMessage(pid, message_data);
}

// Registers the message handler to call when a specific message is received.
void RegisterMessageHandler(
    MessageId message_id,
    std::function<void(ProcessId, const MessageData&)> callback) {
  RegisterRawMessageHandler(
      message_id, [callback = std::move(callback)](
                      ProcessId sender, const MessageData& message_data) {
        if (message_data.metadata != 0) {
          // This is an RPC, and not something a basic message handler should
          // deal with.
          DealWithUnhandledMessage(sender, message_data);
          return;
        }
        callback(sender, message_data);
      });
}

// Registers the message handler to call when a specific message is received.
// Assigning another handler to the same Message ID will override that handler.
// If you don't know what you're doing and don't handle memory pages that are
// sent to you, this can lead to memory leaks.
void RegisterRawMessageHandler(
    MessageId message_id,
    std::function<void(ProcessId, const MessageData&)> callback) {
  auto handler = std::make_shared<MessageHandler>();
  handler->fiber_to_wake_up = nullptr;
  handler->handler_function = std::move(callback);

  SpinlockLock lock(GetMessagesLock());
  // Erase already existing message handler.
  auto& handlers = GetHandlersByMessageId();
  auto handlers_by_message_id_itr = handlers.find(message_id);
  if (handlers_by_message_id_itr != handlers.end())
    handlers.erase(handlers_by_message_id_itr);

  handlers.emplace(
      std::make_pair(message_id, std::move(handler)));
}

// Unregisters the message handler.
void UnregisterMessageHandler(MessageId message_id) {
  SpinlockLock lock(GetMessagesLock());
  auto& handlers = GetHandlersByMessageId();
  auto handlers_by_message_id_itr = handlers.find(message_id);
  if (handlers_by_message_id_itr != handlers.end())
    handlers.erase(handlers_by_message_id_itr);
}

// Sleeps the current fiber until a message is received. Waiting for a message
// with a handler assigned to it will override that handler.
void SleepUntilMessage(MessageId message_id, ProcessId& sender,
                       MessageData& message_data) {
  SleepUntilRawMessage(message_id, sender, message_data);
  if (message_data.metadata != 0) {
    // This is an RPC, and not something a basic message handler should
    // deal with.
    DealWithUnhandledMessage(sender, message_data);
    message_data = {};
  }
}

// Sleeps the current fiber until a message is received. Waiting for a message
// with a handler assigned to it will override that handler. If memory pages are
// sent and not handled, this can lead to memory leaks.
void SleepUntilRawMessage(MessageId message_id, ProcessId& sender,
                          MessageData& message_data) {
  // Register the handler to wake up the fiber.
  auto handler = std::make_shared<MessageHandler>();
  handler->fiber_to_wake_up = GetCurrentlyExecutingFiber();

  {
    SpinlockLock lock(GetMessagesLock());
    GetHandlersByMessageId().emplace(
        std::make_pair(message_id, std::move(handler)));
  }

  // Yield this fiber.
  Sleep();

  // Get the handler.
  SpinlockLock lock(GetMessagesLock());
  auto& handlers = GetHandlersByMessageId();
  auto handler_itr = handlers.find(message_id);
  if (handler_itr == handlers.end()) {
    // This should never happen, but we'll have to return something.
    message_data = {};
    return;
  }

  sender = handler_itr->second->senders_pid;
  message_data = handler_itr->second->message_data;

  // Stop listening.
  handlers.erase(handler_itr);
}

void RegisterWakeUpHandler(MessageId message_id) {
  auto handler = std::make_shared<MessageHandler>();
  handler->fiber_to_wake_up = GetCurrentlyExecutingFiber();

  SpinlockLock lock(GetMessagesLock());
  GetHandlersByMessageId().emplace(
      std::make_pair(message_id, std::move(handler)));
}

void SleepAndGetRawMessage(MessageId message_id, ProcessId& sender,
                           MessageData& message_data) {
  // Yield this fiber.
  Sleep();

  // Get the handler.
  SpinlockLock lock(GetMessagesLock());
  auto& handlers = GetHandlersByMessageId();
  auto handler_itr = handlers.find(message_id);
  if (handler_itr == handlers.end()) {
    // This should never happen, but we'll have to return something.
    sender = 0;
    message_data = {};
    return;
  }

  sender = handler_itr->second->senders_pid;
  message_data = handler_itr->second->message_data;

  // Stop listening.
  handlers.erase(handler_itr);
}

// Maybe returns a message handler for the given ID, or nullptr.
std::shared_ptr<MessageHandler> GetMessageHandler(MessageId message_id) {
  SpinlockLock lock(GetMessagesLock());
  auto& handlers = GetHandlersByMessageId();
  auto handler_itr = handlers.find(message_id);
  if (handler_itr == handlers.end())
    return {};
  else
    return handler_itr->second;
}

}  // namespace perception
