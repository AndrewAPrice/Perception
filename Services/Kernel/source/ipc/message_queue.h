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

#pragma once

#include "../../../Libraries/perception/public/status.h"
#include "containers/linked_list.h"
#include "containers/spinlock.h"
#include "ipc/messages.h"
#include "scheduling/thread.h"
#include "types.h"

namespace processes {
struct Process;
}

namespace ipc {

// Maximum number of messages that can be queued for a process.
constexpr size_t kDefaultMaxMessagesQueued = 1024;

// Manages message delivery, queued messages, and waiting threads for a process.
class MessageQueue {
 public:
  // Constructs a message queue owned by the given process.
  MessageQueue(processes::Process& owner,
               size_t max_capacity = kDefaultMaxMessagesQueued);

  // Destructor that clears any remaining queued messages.
  ~MessageQueue();

  // Delivers a message directly to a waiting thread or queues it.
  // Assumes lock() is held. Populates waiting_thread if a thread was awoken.
  Status DeliverOrQueueLocked(size_t message_id, size_t sender_pid,
                              size_t metadata, size_t param1, size_t param2,
                              size_t param3, size_t param4, size_t param5,
                              scheduling::Thread*& waiting_thread);

  // Pops the next queued message into thread registers, or puts the thread
  // to sleep waiting for a message. Returns true if the thread went to sleep.
  bool PopIntoThreadOrSleep(scheduling::Thread& thread);

  // Pops and returns the next queued message, or nullptr if empty.
  // Assumes lock() is held.
  Message* PopNextLocked();

  // Removes a thread from the sleeping list.
  void RemoveSleepingThread(scheduling::Thread& thread);

  // Frees all queued messages back to the object pool.
  void Clear();

  // Returns whether the queue has reached capacity.
  bool is_full() const { return count_ >= max_capacity_; }

  // Returns whether the queue has no messages.
  bool is_empty() const { return count_ == 0; }

  // Returns the number of messages currently queued.
  size_t count() const { return count_; }

  // Spinlock synchronizing message delivery and retrieval.
  containers::InterruptSafeSpinlock& lock() { return lock_; }

  // Threads waiting for a message.
  containers::LinkedList<scheduling::Thread,
                         &scheduling::Thread::node_sleeping_for_messages>&
  sleeping_threads() {
    return sleeping_threads_;
  }

  // Queued messages waiting to be consumed.
  containers::LinkedList<Message, &Message::node>& queued_messages() {
    return messages_;
  }

 private:
  processes::Process& owner_;
  containers::InterruptSafeSpinlock lock_;
  containers::LinkedList<Message, &Message::node> messages_;
  containers::LinkedList<scheduling::Thread,
                         &scheduling::Thread::node_sleeping_for_messages>
      sleeping_threads_;
  size_t count_;
  size_t max_capacity_;
};

}  // namespace ipc
