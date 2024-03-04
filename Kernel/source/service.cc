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

namespace {

LinkedList<ProcessToNotifyWhenServiceAppears,
           &ProcessToNotifyWhenServiceAppears::node_in_all_notifiations>
    processes_to_be_notified_when_a_service_appears;

}  // namespace

// Initializes the internal structures for tracking services.
void InitializeServices() {
  new (&processes_to_be_notified_when_a_service_appears) LinkedList<
      ProcessToNotifyWhenServiceAppears,
      &ProcessToNotifyWhenServiceAppears::node_in_all_notifiations>();
}

// Do two service names (of length SERVICE_NAME_LENGTH) match?
bool DoServiceNamesMatch(const char* a, const char* b) {
  for (int word = 0; word < SERVICE_NAME_WORDS; word++)
    if (((size_t*)a)[word] != ((size_t*)b)[word]) return false;

  return true;
}

// Registers a service, and notifies anybody listening for new instances
// of services with this name.
void RegisterService(char* service_name, Process* process, size_t message_id) {
  auto service = ObjectPool<Service>::Allocate();
  if (service == nullptr) return;  // Out of memory.
  service->process = process;
  service->message_id = message_id;
  for (int i = 0; i < SERVICE_NAME_WORDS; i++)
    ((size_t*)service->name)[i] = ((size_t*)service_name)[i];

  // Add to the linked list of services in the process.
  if (process->services.IsEmpty()) {
    process->services.AddBack(service);
  } else {
    // FindNextServiceByPidAndMidWithName depends on the services being
    // sorted in order of their message id. There could exist a scenario
    // (such as a race condition) where services get registered out of
    // order. We'll walk backwards from the end to find where to insert
    // us.
    Service* previous_service = process->services.LastItem();
    while (previous_service != nullptr &&
           service->message_id < previous_service->message_id) {
      previous_service = process->services.PreviousItem(previous_service);
    }

    if (service->message_id == previous_service->message_id) {
      // Trying to register multiple services with the same message ID.
      ObjectPool<Service>::Release(service);
      return;
    }
    process->services.InsertAfter(previous_service, service);
  }

  // Notify everyone listening for this new service.
  for (ProcessToNotifyWhenServiceAppears* notification :
       processes_to_be_notified_when_a_service_appears) {
    if (DoServiceNamesMatch(service_name, notification->service_name)) {
      SendKernelMessageToProcess(notification->process,
                                 notification->message_id, process->pid,
                                 message_id, 0, 0, 0);
    }
  }
}

// Unregisters a service, and notifies anybody listening.
void UnregisterServiceByMessageId(Process* process, size_t message_id) {
  for (Service* service : process->services) {
    if (message_id == service->message_id) return UnregisterService(service);
    if (message_id > service->message_id) return;  // Short circuit early.
  }
}

// Unregisters a service, and notifies anybody listening.
void UnregisterService(Service* service) {
  while (!service->processes_to_notify_on_disappear.IsEmpty()) {
    auto* notification = service->processes_to_notify_on_disappear.PopFront();
    SendKernelMessageToProcess(notification->process, notification->message_id,
                               0, 0, 0, 0, 0);

    service->processes_to_notify_on_disappear.Remove(notification);
    notification->process->services_i_want_to_be_notified_of_when_they_disappear
        .Remove(notification);
    ObjectPool<ProcessToNotifyWhenServiceDisappears>::Release(notification);
  }
  service->process->services.Remove(service);
  ObjectPool<Service>::Release(service);
}

Service* FindServiceByProcessAndMid(size_t pid, size_t message_id) {
  Process* process = GetProcessFromPid(pid);
  if (process == nullptr) return nullptr;  // Process doesn't exist.

  for (Service* service : process->services)
    if (service->message_id == message_id) return service;

  return nullptr;  // Service doesn't exist in process.
}

// Returns the next service, starting at the provided process ID and message
// ID.
Service* FindNextServiceByPidAndMidWithName(char* service_name, size_t min_pid,
                                            size_t min_message_id) {
  Process* process = GetProcessOrNextFromPid(min_pid);

  // We only care about starting from this mid if we are continuing
  // from the same process.
  if (process->pid != min_pid) min_message_id = 0;

  // We'll return if we find the next process, otherwise keep
  // looping and scanning the next processes for services.
  while (process != nullptr) {
    // Keep looping through services in this process.
    for (Service* service : process->services) {
      // Does this message meet our min message id threshold
      // and also have the same service name that we're looking
      // for?
      if (service->message_id >= min_message_id &&
          (service_name[0] == 0 ||
           DoServiceNamesMatch(service_name, service->name))) {
        // We found a service we're looking for!
        return service;
      }
    }

    // Jump to the next process, and reset the min mid we care
    // about.
    process = GetNextProcess(process);
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
  Service* service = process->services.NextItem(previous_service);

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
      service = process->services.NextItem(previous_service);
    }

    // Jump to the next process.
    process = GetNextProcess(process);
    if (process != nullptr) service = process->services.FirstItem();
  }
  return nullptr;
}

// Registers that we want this process to be notified when a service of the
// given service name appears. This also sends a notification for all existing
// services with the given service name.
void NotifyProcessWhenServiceAppears(char* service_name, Process* process,
                                     size_t message_id) {
  auto notification = ObjectPool<ProcessToNotifyWhenServiceAppears>::Allocate();
  if (notification == nullptr) return;  // Out of memory.

  // Conthe object.
  notification->process = process;
  notification->message_id = message_id;
  for (int i = 0; i < SERVICE_NAME_WORDS; i++)
    ((size_t*)notification->service_name)[i] = ((size_t*)service_name)[i];

  // Add to global linked list.
  processes_to_be_notified_when_a_service_appears.AddBack(notification);
  // Add to linked list in process.
  process->services_i_want_to_be_notified_of_when_they_appear.AddBack(
      notification);

  // Loop through all services and send the process a message for any that
  // match the name we are listening for.
  Process* process_to_scan = GetProcessOrNextFromPid(0);
  while (process_to_scan != nullptr) {
    for (Service* service : process_to_scan->services) {
      if (DoServiceNamesMatch(service_name, service->name)) {
        SendKernelMessageToProcess(process, message_id, service->process->pid,
                                   service->message_id, 0, 0, 0);
      };
    }
    process_to_scan = GetNextProcess(process_to_scan);
  }
}

// Registers that we no longer want to be notified when a service appears.
void StopNotifyingProcessWhenServiceAppearsByMessageId(Process* process,
                                                       size_t message_id) {
  // There might be multiple notification with the same message ID,
  // so we will unregister them all.
  for (auto* notification =
           process->services_i_want_to_be_notified_of_when_they_appear
               .FirstItem();
       notification != nullptr;) {
    auto* next_notification =
        process->services_i_want_to_be_notified_of_when_they_appear.NextItem(
            notification);
    if (notification->message_id == message_id)
      StopNotifyingProcessWhenServiceAppears(notification);

    notification = next_notification;
  }
}

// Registers that we no longer want to be notified when a service appears.
void StopNotifyingProcessWhenServiceAppears(
    ProcessToNotifyWhenServiceAppears* notification) {
  // Remove from global linked list.
  processes_to_be_notified_when_a_service_appears.Remove(notification);
  // Remove from process's linked list.
  notification->process->services_i_want_to_be_notified_of_when_they_appear
      .Remove(notification);
  // Release this notification.
  ObjectPool<ProcessToNotifyWhenServiceAppears>::Release(notification);
}

// Registers that a process wants to be notified when a service disappears.
void NotifyProcessWhenServiceDisappears(Process* process,
                                       size_t service_process_id,
                                       size_t service_message_id,
                                       size_t message_id) {
  auto* service =
      FindServiceByProcessAndMid(service_process_id, service_message_id);

  if (service) {
    auto notification =
        ObjectPool<ProcessToNotifyWhenServiceDisappears>::Allocate();
    if (notification == nullptr) return;  // Out of memory.

    notification->process = process;
    notification->service = service;
    notification->message_id = message_id;

    process->services_i_want_to_be_notified_of_when_they_disappear.AddBack(
        notification);
    service->processes_to_notify_on_disappear.AddBack(notification);
  } else {
    // The service doesn't exist so send the message immediately.
    SendKernelMessageToProcess(process, message_id, 0, 0, 0, 0, 0);
  }
}

// Unregisters that a process wants to be notified when a service disappears.
void StopNotifyingProcessWhenServiceDisappears(Process* process,
                                               size_t message_id) {
  ProcessToNotifyWhenServiceDisappears* notification = nullptr;
  for (auto n :
       process->services_i_want_to_be_notified_of_when_they_disappear) {
    if (n->message_id == message_id) {
      notification = n;
      break;
    }
  }

  if (notification) StopNotifyingProcessWhenServiceDisappears(notification);
}

// Unregisters that a process wants to be notified when a service disappears.
void StopNotifyingProcessWhenServiceDisappears(
    ProcessToNotifyWhenServiceDisappears* notification) {
  notification->service->processes_to_notify_on_disappear.Remove(notification);
  notification->process->services_i_want_to_be_notified_of_when_they_disappear
      .Remove(notification);
  ObjectPool<ProcessToNotifyWhenServiceDisappears>::Release(notification);
}
