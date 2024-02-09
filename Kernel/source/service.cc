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

#include "service.h"

#include "messages.h"
#include "object_pool.h"
#include "process.h"

ProcessToNotifyWhenServiceAppears*
    first_process_to_be_notified_when_a_service_appears = nullptr;

// Initializes the internal structures for tracking services.
void InitializeServices() {
  first_process_to_be_notified_when_a_service_appears = nullptr;
}

// Do two service names (of length SERVICE_NAME_LENGTH) match?
bool DoServiceNamesMatch(const char* a, const char* b) {
  for (int word = 0; word < SERVICE_NAME_WORDS; word++) {
    if (((size_t*)a)[word] != ((size_t*)b)[word]) return false;
  }

  return true;
}

// Registers a service, and notifies anybody listening for new instances
// of services with this name.
void RegisterService(char* service_name, Process* process,
                     size_t message_id) {
  auto service = ObjectPool<Service>::Allocate();
  if (service == nullptr) return;  // Out of memory.
  service->process = process;
  service->message_id = message_id;
  for (int i = 0; i < SERVICE_NAME_WORDS; i++)
    ((size_t*)service->name)[i] = ((size_t*)service_name)[i];

  // Add to the linked list of services in the process.
  if (process->first_service == nullptr) {
    // This is the process's only service.
    service->previous_service_in_process = nullptr;
    service->next_service_in_process = nullptr;
    process->first_service = service;
    process->last_service = service;
  } else {
    // FindNextServiceByPidAndMidWithName depends on the services being
    // sorted in order of their message id. There could exist a scenario
    // (such as a race condition) where services get registered out of
    // order. We'll walk backwards from the end to find where to insert
    // us.
    Service* previous_service = process->last_service;
    while (previous_service != nullptr &&
           service->message_id < previous_service->message_id) {
      previous_service = previous_service->previous_service_in_process;
    }

    if (service->message_id == previous_service->message_id) {
      // Trying to register multiple services with the same message ID.
      ObjectPool<Service>::Release(service);
      return;
    }

    if (previous_service == nullptr) {
      // Add us to the beginning.
      service->previous_service_in_process = nullptr;
      service->next_service_in_process = process->first_service;
      process->first_service->previous_service_in_process = service;
      process->first_service = service;
    } else if (previous_service == process->last_service) {
      // Add us to the end.
      service->previous_service_in_process = process->last_service;
      process->last_service->next_service_in_process = service;
      service->next_service_in_process = nullptr;
      process->last_service = service;
    } else {
      // Slot us between two processes.
      service->previous_service_in_process = previous_service;
      previous_service->next_service_in_process = service;
      service->next_service_in_process =
          previous_service->next_service_in_process;
      service->next_service_in_process->previous_service_in_process = service;
    }
  }

  // Notify everyone listening for this new service.
  ProcessToNotifyWhenServiceAppears* notification =
      first_process_to_be_notified_when_a_service_appears;
  while (notification != nullptr) {
    if (DoServiceNamesMatch(service_name, notification->service_name)) {
      SendKernelMessageToProcess(notification->process,
                                 notification->message_id, process->pid,
                                 message_id, 0, 0, 0);
    }
    notification = notification->next_notification;
  }
}

// Unregisters a service, and notifies anybody listening.
void UnregisterServiceByMessageId(Process* process, size_t message_id) {
  Service* service = process->first_service;
  while (service != nullptr && message_id <= service->message_id) {
    if (message_id == service->message_id) {
      return UnregisterService(service);
    }
    service = service->next_service_in_process;
  }
}

// Unregisters a service, and notifies anybody listening.
void UnregisterService(Service* service) {
  // TODO: Notify everyone listening for this service to die.

  // Remove from the linked list of services in the process.
  if (service->previous_service_in_process == nullptr)
    // We are the first service.
    service->process->first_service = service->next_service_in_process;
  else
    // There is a service before us.
    service->previous_service_in_process->next_service_in_process =
        service->next_service_in_process;

  if (service->next_service_in_process == nullptr)
    // We are the last service.
    service->process->last_service = service->previous_service_in_process;
  else
    // There is a service before us.
    service->next_service_in_process->previous_service_in_process =
        service->previous_service_in_process;

  ObjectPool<Service>::Release(service);
}

Service* FindServiceByProcessAndMid(size_t pid, size_t message_id) {
  Process* process = GetProcessFromPid(pid);
  if (process == nullptr) return nullptr;  // Process doesn't exist.

  for (Service* service = process->first_service; service != nullptr;
       service = service->next_service_in_process) {
    if (service->message_id == message_id)
      return service;  // Found the service we want.
  }

  return nullptr;  // Service doesn't exist in process.
}

// Returns the next service, starting at the provided process ID and message
// ID.
Service* FindNextServiceByPidAndMidWithName(char* service_name,
                                                   size_t min_pid,
                                                   size_t min_message_id) {
  Process* process = GetProcessOrNextFromPid(min_pid);

  // We only care about starting from this mid if we are continuing
  // from the same process.
  if (process->pid != min_pid) min_message_id = 0;

  // We'll return if we find the next process, otherwise keep
  // looping and scanning the next processes for services.
  while (process != nullptr) {
    Service* service = process->first_service;
    // Keep looping through services in this process.
    while (service != nullptr) {
      // Does this message meet our min message id threshold
      // and also have the same service name that we're looking
      // for?
      if (service->message_id >= min_message_id &&
          (service_name[0] == 0 ||
           DoServiceNamesMatch(service_name, service->name))) {
        // We found a service we're looking for!
        return service;
      }
      service = service->next_service_in_process;
    }

    // Jump to the next process, and reset the min mid we care
    // about.
    process = process->next;
    min_message_id = 0;
  }

  // Couldn't find any more services with this name.
  return nullptr;
}

// Returns the next service, or nullptr if there are no more services.
Service* FindNextServiceWithName(char* service_name,
                                        Service* previous_service) {
  // We're out of services.
  if (previous_service == nullptr) return nullptr;

  // Remember the process we're starting from.
  Process* process = previous_service->process;

  // Start scanning from the next service, so we don't return
  // the service passed as input.
  Service* service = previous_service->next_service_in_process;

  // While we still have processes.
  while (process != nullptr) {
    // While we still have in this process.
    while (service != nullptr) {
      // Does this service have the same name that we're
      // looking for?
      if (service_name[0] == 0 ||
          DoServiceNamesMatch(service_name, service->name)) {
        // We found a service we're looking for!
        return service;
      }
      // Jump to the next service.
      service = service->next_service_in_process;
    }

    // Jump to the next process.
    process = process->next;
    if (process != nullptr) service = process->first_service;
  }
  return nullptr;
}

// Registers that we want this process to be notified when a service of the
// given service name appears. This also sends a notification for all existing
// services with the given service name.
void NotifyProcessWhenServiceAppears(char* service_name,
                                     Process* process,
                                     size_t message_id) {
  auto notification =
      ObjectPool<ProcessToNotifyWhenServiceAppears>::Allocate();
  if (notification == nullptr) return;  // Out of memory.

  // Conthe object.
  notification->process = process;
  notification->message_id = message_id;
  for (int i = 0; i < SERVICE_NAME_WORDS; i++)
    ((size_t*)notification->service_name)[i] = ((size_t*)service_name)[i];

  // Add to global linked list.
  notification->previous_notification = nullptr;
  if (first_process_to_be_notified_when_a_service_appears == nullptr) {
    notification->next_notification = nullptr;
  } else {
    notification->next_notification =
        first_process_to_be_notified_when_a_service_appears;
    first_process_to_be_notified_when_a_service_appears->previous_notification =
        notification;
  }
  first_process_to_be_notified_when_a_service_appears = notification;

  // Add to linked list in process.
  notification->previous_notification_in_process = nullptr;
  if (process->services_i_want_to_be_notified_of_when_they_appear == nullptr) {
    notification->next_notification_in_process = nullptr;
  } else {
    notification->next_notification_in_process =
        process->services_i_want_to_be_notified_of_when_they_appear;
    process->services_i_want_to_be_notified_of_when_they_appear
        ->previous_notification_in_process = notification;
  }
  process->services_i_want_to_be_notified_of_when_they_appear = notification;

  // Loop through all services and send the process a message for any that
  // match the name we are listening for.
  Process* process_to_scan = GetProcessOrNextFromPid(0);
  while (process_to_scan != nullptr) {
    Service* service = process_to_scan->first_service;
    while (service != nullptr) {
      if (DoServiceNamesMatch(service_name, service->name)) {
        SendKernelMessageToProcess(process, message_id, service->process->pid,
                                   service->message_id, 0, 0, 0);
      }

      service = service->next_service_in_process;
    }
    process_to_scan = process_to_scan->next;
  }
}

// Registers that we no longer want to be notified when a service appears.
void StopNotifyingProcessWhenServiceAppearsByMessageId(Process* process,
                                                       size_t message_id) {
  ProcessToNotifyWhenServiceAppears* notification =
      process->services_i_want_to_be_notified_of_when_they_appear;

  // There might be multiple notification with the same message ID,
  // so we will unregister them all.
  while (notification != nullptr) {
    ProcessToNotifyWhenServiceAppears* next_notification =
        notification->next_notification_in_process;
    if (notification->message_id == message_id)
      StopNotifyingProcessWhenServiceAppears(notification);

    notification = next_notification;
  }
}

// Registers that we no longer want to be notified when a service appears.
void StopNotifyingProcessWhenServiceAppears(
    ProcessToNotifyWhenServiceAppears* notification) {
  // Remove from global linked list.
  if (notification->previous_notification == nullptr) {
    first_process_to_be_notified_when_a_service_appears =
        notification->next_notification;
  } else {
    notification->previous_notification->next_notification =
        notification->next_notification;
  }
  if (notification->next_notification != nullptr) {
    notification->next_notification->previous_notification =
        notification->previous_notification;
  }

  // Remove from process's linked list.
  if (notification->previous_notification_in_process == nullptr) {
    notification->process->services_i_want_to_be_notified_of_when_they_appear =
        notification->next_notification_in_process;
  } else {
    notification->previous_notification_in_process
        ->next_notification_in_process =
        notification->next_notification_in_process;
  }
  if (notification->next_notification_in_process != nullptr) {
    notification->next_notification_in_process
        ->previous_notification_in_process =
        notification->previous_notification_in_process;
  }

  // Release this notification.
  ObjectPool<ProcessToNotifyWhenServiceAppears>::Release(notification);
}
