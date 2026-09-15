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

#pragma once

#include "containers/aa_tree.h"
#include "containers/linked_list.h"
#include "types.h"

namespace processes {
struct Process;
}

namespace ipc {

struct Service;

// Length of a service name in 64-bit words.
constexpr size_t kServiceNameWords = 9;

// Maximum length of a service name in characters.
constexpr size_t kServiceNameLength = kServiceNameWords * 8;

// Represents a process to notify when a service appears.
struct ProcessToNotifyWhenServiceAppears {
  // The service name we're waiting for.
  char service_name[kServiceNameLength];

  // The process to notify.
  processes::Process* process;

  // The message ID to send a message to when this process appears.
  size_t message_id;

  // Linked list in the global list of notifications.
  containers::LinkedListNode node_in_all_notifiations;

  // Linked list in the process.
  containers::LinkedListNode node_in_process;
};

// Represents a process to notify when a service disappears.
struct ProcessToNotifyWhenServiceDisappears {
  // The process to notify.
  processes::Process* process;

  // The service being watched.
  Service* service;

  // The message ID to send a message to when this service disappears.
  size_t message_id;

  // Linked list in the process.
  containers::LinkedListNode node_in_process;

  // Linked list in the service.
  containers::LinkedListNode node_in_service;
};

// Represents a registered service.
struct Service {
  // The process this service belongs to.
  processes::Process* process;

  // Message ID to use for communicating to this service.
  size_t message_id;

  // The name of the service.
  char name[kServiceNameLength + 1];

  // AA tree node of registered services in this process.
  containers::AATreeNode node_in_process;

  // Linked list of processes to notify when this disappears.
  containers::LinkedList<ProcessToNotifyWhenServiceDisappears,
                         &ProcessToNotifyWhenServiceDisappears::node_in_service>
      processes_to_notify_on_disappear;
};

// Initializes the internal structures for tracking services.
void InitializeServices();

// Registers a service, and notifies anybody listening for new instances
// of services with this name.
void RegisterService(char* service_name, processes::Process* process,
                     size_t message_id);

// Unregisters a service, and notifies anybody listening.
void UnregisterServiceByMessageId(processes::Process* process,
                                  size_t message_id);

// Unregisters a service, and notifies anybody listening.
void UnregisterService(Service* service);

// Returns a service running in a process with the matching message id, or
// nullptr if it does not exist.
Service* FindServiceByProcessAndMid(size_t pid, size_t message_id);

// Queries services matching service_name starting from min_pid and min_message_id.
// Populates pids and sids up to max_results under a single lock acquisition.
// Returns total number of matching services found.
size_t QueryServices(const char* service_name, size_t min_pid,
                     size_t min_message_id, size_t* pids, size_t* sids,
                     size_t max_results);

// Safely copies the name of a service into name_out under the directory lock.
// Returns true if the service was found.
bool GetServiceName(size_t pid, size_t message_id, char* name_out);

// Returns the next service, starting at the provided process ID and message
// ID.
Service* FindNextServiceByPidAndMidWithName(char* service_name, size_t min_pid,
                                            size_t min_message_id);

// Returns the next service, or nullptr if there are no more services.
Service* FindNextServiceWithName(char* service_name, Service* previous_service);

// Registers a process to be notified when a service of the
// given service name appears. This also sends a notification for all existing
// services with the given service name.
void NotifyProcessWhenServiceAppears(char* service_name,
                                     processes::Process* process,
                                     size_t message_id);

// Unregisters notification for when a service appears by message ID.
void StopNotifyingProcessWhenServiceAppearsByMessageId(
    processes::Process* process, size_t message_id);

// Unregisters notification for when a service appears.
void StopNotifyingProcessWhenServiceAppears(
    ProcessToNotifyWhenServiceAppears* notification);

// Registers that a process wants to be notified when a service disappears.
void NotifyProcessWhenServiceDisappears(processes::Process* process,
                                        size_t service_process_id,
                                        size_t service_message_id,
                                        size_t message_id);

// Unregisters that a process wants to be notified when a service disappears.
void StopNotifyingProcessWhenServiceDisappears(processes::Process* process,
                                               size_t message_id);

// Unregisters that a process wants to be notified when a service disappears.
void StopNotifyingProcessWhenServiceDisappears(
    ProcessToNotifyWhenServiceDisappears* notification);

}  // namespace ipc

