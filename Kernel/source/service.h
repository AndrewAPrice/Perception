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

#include "linked_list.h"
#include "types.h"

struct Process;
struct Service;

// Maximum length of a service.
#define SERVICE_NAME_WORDS 10
#define SERVICE_NAME_LENGTH (SERVICE_NAME_WORDS * 8)

// Represents a process to notify when a service appears.
struct ProcessToNotifyWhenServiceAppears {
  // The service name we're waiting for.
  char service_name[SERVICE_NAME_LENGTH];

  // The process to notify.
  Process* process;

  // The message ID to send a message to when this process appears.
  size_t message_id;

  // Linked list in the global list of notifications.
  LinkedListNode node_in_all_notifiations;

  // Linked list in the process.
  LinkedListNode node_in_process;
};

// Represents a process to nofy when a service disappears.
struct ProcessToNotifyWhenServiceDisappears {
  // The process to notify.
  Process* process;

  // The service that we're watching..
  Service* service;

  // The message ID to send a message to when this process appears.
  size_t message_id;

  // Linked list in the process.
  LinkedListNode node_in_process;

  // Linked list in the service.
  LinkedListNode node_in_service;
};

// Represents a registered service.
struct Service {
  // The process this service belongs to.
  Process* process;

  // Message ID to use for communicating to this service.
  size_t message_id;

  // The name of the service.
  char name[SERVICE_NAME_LENGTH];

  // Linked list of registered services in this process.
  LinkedListNode node_in_process;

  // Linked list of processes to notify when this disappears.
  LinkedList<ProcessToNotifyWhenServiceDisappears,
             &ProcessToNotifyWhenServiceDisappears::node_in_service>
      processes_to_notify_on_disappear;
};

// Initializes the internal structures for tracking services.
void InitializeServices();

// Registers a service, and notifies anybody listening for new instances
// of services with this name.
void RegisterService(char* service_name, Process* process, size_t message_id);

// Unregisters a service, and notifies anybody listening.
void UnregisterServiceByMessageId(Process* process, size_t message_id);

// Unregisters a service, and notifies anybody listening.
void UnregisterService(Service* service);

// Returns a service running in a process with the matching message id, or
// nullptr if it does not exist.
Service* FindServiceByProcessAndMid(size_t pid, size_t message_id);

// Returns the next service, starting at the provided process ID and message
// ID.
Service* FindNextServiceByPidAndMidWithName(char* service_name, size_t min_pid,
                                            size_t min_message_id);

// Returns the next service, or nullptr if there are no more services.
Service* FindNextServiceWithName(char* service_name, Service* previous_service);

// Registers that we want this process to be notified when a service of the
// given service name appears. This also sends a notification for all existing
// services with the given service name.
void NotifyProcessWhenServiceAppears(char* service_name, Process* process,
                                     size_t message_id);

// Registers that we no longer want to be notified when a service appears.
void StopNotifyingProcessWhenServiceAppearsByMessageId(Process* process,
                                                       size_t message_id);

// Registers that we no longer want to be notified when a service appears.
void StopNotifyingProcessWhenServiceAppears(
    ProcessToNotifyWhenServiceAppears* notification);

// Registers that a process wants to be notified when a service disappears.
void NotifyProcessWhenServiceDisappears(Process* process,
                                        size_t service_process_id,
                                        size_t service_message_id,
                                        size_t message_id);

// Unregisters that a process wants to be notified when a service disappears.
void StopNotifyingProcessWhenServiceDisappears(Process* process,
                                               size_t message_id);

// Unregisters that a process wants to be notified when a service disappears.
void StopNotifyingProcessWhenServiceDisappears(
    ProcessToNotifyWhenServiceDisappears* notification);
