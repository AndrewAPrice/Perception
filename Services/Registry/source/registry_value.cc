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

#include "registry_value.h"

#include <map>
#include <mutex>
#include <unordered_map>

#include "status.h"

using ::perception::MessageId;
using ::perception::ProcessId;

namespace {

// Uniquely identifies a listener subscription.
struct ListenerKey {
  ProcessId process_id;
  MessageId message_id;

  bool operator==(const ListenerKey& other) const {
    return process_id == other.process_id && message_id == other.message_id;
  }
};

// Hash function for ListenerKey to enable O(1) hash map lookups.
struct ListenerKeyHash {
  std::size_t operator()(const ListenerKey& k) const {
    return std::hash<uint64_t>()(static_cast<uint64_t>(k.process_id)) ^
           (std::hash<uint64_t>()(static_cast<uint64_t>(k.message_id)) << 1);
  }
};

// Global lock protecting all listener registries, maps, and tracking vectors.
std::mutex listeners_mutex;

// Maps a unique listener key to the specific RegistryValue it is subscribing
// to. This allows O(1) resolution and unregistration of listeners.
std::unordered_map<ListenerKey, RegistryValue*, ListenerKeyHash>
    global_listeners;

// Maps a ProcessId to all MessageIds representing active listener subscriptions
// for that process.
std::map<ProcessId, std::vector<MessageId>> process_listeners;

// Maps a ProcessId to the termination notification registration token.
std::map<ProcessId, MessageId> termination_handlers;

// Removes a listener subscription from its associated RegistryValue object.
void RemoveListenerFromValueLocked(ProcessId process_id, MessageId message_id) {
  auto it = global_listeners.find({process_id, message_id});
  if (it != global_listeners.end()) {
    RegistryValue* val = it->second;
    val->RemoveListener(process_id, message_id);
    global_listeners.erase(it);
  }
}

// Cleans up process-tracking collections when a listener is removed or a
// process dies. Automatically cancels termination notifications once a process
// has no active listeners.
void RemoveProcessListenerLocked(ProcessId process_id, MessageId message_id) {
  auto pit = process_listeners.find(process_id);
  if (pit != process_listeners.end()) {
    auto& list = pit->second;
    for (auto mit = list.begin(); mit != list.end(); ++mit) {
      if (*mit == message_id) {
        list.erase(mit);
        break;
      }
    }
    if (list.empty()) {
      auto hit = termination_handlers.find(process_id);
      if (hit != termination_handlers.end()) {
        ::perception::StopNotifyingUponProcessTermination(hit->second);
        termination_handlers.erase(hit);
      }
      process_listeners.erase(pit);
    }
  }
}

// Unregisters a single listener subscription completely under the active lock.
void UnregisterListenerLocked(ProcessId process_id, MessageId message_id) {
  RemoveListenerFromValueLocked(process_id, message_id);
  RemoveProcessListenerLocked(process_id, message_id);
}

}  // namespace

RegistryValue::~RegistryValue() {
  std::scoped_lock lock(listeners_mutex);
  for (const auto& listener : listeners_) {
    global_listeners.erase({listener.process_id, listener.message_id});
    RemoveProcessListenerLocked(listener.process_id, listener.message_id);
  }
}

void RegistryValue::RegisterListener(ProcessId process_id,
                                     MessageId message_id) {
  std::scoped_lock lock(listeners_mutex);
  listeners_.push_back({process_id, message_id});
  global_listeners[{process_id, message_id}] = this;

  auto& list = process_listeners[process_id];
  list.push_back(message_id);

  // If this is the first listener for this process, subscribe to process
  // termination events so it be automatically cleaned up when the process
  // terminates.
  if (list.size() == 1) {
    MessageId mid =
        ::perception::NotifyUponProcessTermination(process_id, [process_id]() {
          std::scoped_lock lock(listeners_mutex);
          auto it = process_listeners.find(process_id);
          if (it != process_listeners.end()) {
            for (MessageId msg_id : it->second) {
              RemoveListenerFromValueLocked(process_id, msg_id);
            }
            process_listeners.erase(it);
          }
          termination_handlers.erase(process_id);
        });
    termination_handlers[process_id] = mid;
  }
}

void RegistryValue::RemoveListener(ProcessId process_id, MessageId message_id) {
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    if (it->process_id == process_id && it->message_id == message_id) {
      listeners_.erase(it);
      break;
    }
  }
}

void RegistryValue::NotifyListeners() {
  std::vector<ListenerInfo> listeners_to_notify;
  {
    std::scoped_lock lock(listeners_mutex);
    listeners_to_notify = listeners_;
  }

  if (listeners_to_notify.empty()) return;

  ::perception::MessageData msg = {};
  msg.param1 = static_cast<size_t>(Status::OK);

  std::vector<ListenerInfo> terminated_listeners;

  // Send update messages to all registered listeners. Do this without
  // holding the lock.
  for (const auto& listener : listeners_to_notify) {
    msg.message_id = listener.message_id;
    auto status = ::perception::SendMessage(listener.process_id, msg);
    if (status == Status::PROCESS_DOESNT_EXIST) {
      terminated_listeners.push_back(listener);
    }
  }

  // Clean up any listeners whose processes have ceased to exist.
  if (!terminated_listeners.empty()) {
    std::scoped_lock lock(listeners_mutex);
    for (const auto& listener : terminated_listeners)
      UnregisterListenerLocked(listener.process_id, listener.message_id);
  }
}

void UnregisterListener(ProcessId process_id, MessageId message_id) {
  std::scoped_lock lock(listeners_mutex);
  UnregisterListenerLocked(process_id, message_id);
}
