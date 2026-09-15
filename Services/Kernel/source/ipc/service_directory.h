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

#include "containers/linked_list.h"
#include "containers/spinlock.h"
#include "ipc/service.h"
#include "types.h"

namespace processes {
struct Process;
}

namespace ipc {

// Directory managing service registration, discovery, and lifecycle notifications.
class ServiceDirectory {
 public:
  // Returns the singleton instance of ServiceDirectory.
  static ServiceDirectory& Get();

  // Constructs the ServiceDirectory.
  ServiceDirectory();

  // Initializes internal structures for tracking services.
  void Initialize();

  // Registers a service, and notifies processes listening for new instances.
  void Register(char* service_name, processes::Process* process,
                size_t message_id);

  // Unregisters a service by process and message ID.
  void UnregisterByMessageId(processes::Process* process, size_t message_id);

  // Unregisters a service and notifies listeners that it disappeared.
  void Unregister(Service* service);

  // Finds a service by process ID and message ID.
  Service* FindByProcessAndMid(size_t pid, size_t message_id);

  // Queries services matching name starting from min_pid and min_message_id,
  // filling pids and sids up to max_results under a single lock acquisition.
  // Returns total number of matching services found.
  size_t QueryServices(const char* service_name, size_t min_pid,
                       size_t min_message_id, size_t* pids, size_t* sids,
                       size_t max_results);

  // Safely copies the name of a service into name_out under the directory lock.
  // Returns true if the service was found.
  bool GetServiceName(size_t pid, size_t message_id, char* name_out);

  // Finds the next service matching name starting from min_pid and min_message_id.
  Service* FindNextByPidAndMidWithName(char* service_name, size_t min_pid,
                                       size_t min_message_id);

  // Finds the next service matching name starting after previous_service.
  Service* FindNextWithName(char* service_name, Service* previous_service);

  // Registers a process to be notified when a service with service_name appears.
  void NotifyWhenAppears(char* service_name, processes::Process* process,
                         size_t message_id);

  // Stops notifying a process when a service appears by message ID.
  void StopNotifyingWhenAppearsByMessageId(processes::Process* process,
                                          size_t message_id);

  // Stops notifying a process when a service appears.
  void StopNotifyingWhenAppears(
      ProcessToNotifyWhenServiceAppears* notification);

  // Registers a process to be notified when a service disappears.
  void NotifyWhenDisappears(processes::Process* process,
                            size_t service_process_id,
                            size_t service_message_id,
                            size_t message_id);

  // Stops notifying a process when a service disappears by message ID.
  void StopNotifyingWhenDisappears(processes::Process* process,
                                  size_t message_id);

  // Stops notifying a process when a service disappears.
  void StopNotifyingWhenDisappears(
      ProcessToNotifyWhenServiceDisappears* notification);

  // Returns spinlock protecting service operations.
  containers::RecursiveInterruptSafeSpinlock& lock() { return lock_; }

 private:
  containers::RecursiveInterruptSafeSpinlock lock_;
  containers::LinkedList<
      ProcessToNotifyWhenServiceAppears,
      &ProcessToNotifyWhenServiceAppears::node_in_all_notifiations>
      processes_to_be_notified_when_a_service_appears_;
};

}  // namespace ipc
