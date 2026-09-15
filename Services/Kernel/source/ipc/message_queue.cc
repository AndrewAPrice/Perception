// Copyright 2026 Google LLC
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

#include "ipc/message_queue.h"

#include "containers/object_pool.h"
#include "hardware/registers.h"
#include "processes/process.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"

namespace ipc {

using containers::InterruptSafeSpinlockGuard;
using containers::ObjectPool;
using scheduling::Thread;
using scheduling::ThreadState;

MessageQueue::MessageQueue(processes::Process& owner, size_t max_capacity)
    : owner_(owner), count_(0), max_capacity_(max_capacity) {}

MessageQueue::~MessageQueue() { Clear(); }

Status MessageQueue::DeliverOrQueueLocked(size_t message_id, size_t sender_pid,
                                          size_t metadata, size_t param1,
                                          size_t param2, size_t param3,
                                          size_t param4, size_t param5,
                                          Thread*& waiting_thread) {
  waiting_thread = sleeping_threads_.PopFront();
  if (waiting_thread != nullptr) {
    SetMessageInThread(*waiting_thread, message_id, sender_pid, metadata,
                       param1, param2, param3, param4, param5);
    return Status::OK;
  }

  if (is_full()) return Status::RECEIVERS_QUEUE_IS_FULL;

  Message* message = ObjectPool<Message>::Allocate();
  if (message == nullptr) return Status::OUT_OF_MEMORY;

  message->message_id = message_id;
  message->sender_pid = sender_pid;
  message->metadata = metadata;
  message->param1 = param1;
  message->param2 = param2;
  message->param3 = param3;
  message->param4 = param4;
  message->param5 = param5;

  messages_.AddBack(message);
  count_++;
  owner_.messages_queued = count_;
  return Status::OK;
}

bool MessageQueue::PopIntoThreadOrSleep(Thread& thread) {
  InterruptSafeSpinlockGuard guard(lock_);
  if (!messages_.IsEmpty()) {
    Message* message = messages_.PopFront();
    count_--;
    owner_.messages_queued = count_;
    LoadMessageIntoThread(message, thread);
    return false;
  }

  sleeping_threads_.Remove(&thread);
  sleeping_threads_.AddBack(&thread);
  scheduling::UnscheduleThread(&thread, ThreadState::BlockedOnMessage);
  return true;
}

Message* MessageQueue::PopNextLocked() {
  Message* message = messages_.PopFront();
  if (message != nullptr) {
    count_--;
    owner_.messages_queued = count_;
  }
  return message;
}

void MessageQueue::RemoveSleepingThread(Thread& thread) {
  InterruptSafeSpinlockGuard guard(lock_);
  sleeping_threads_.Remove(&thread);
}

void MessageQueue::Clear() {
  InterruptSafeSpinlockGuard guard(lock_);
  while (Message* message = messages_.PopFront())
    ObjectPool<Message>::Release(message);
  count_ = 0;
  owner_.messages_queued = 0;
}

}  // namespace ipc
