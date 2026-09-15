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

#include "ipc/service_directory.h"

#include "common/kernel_string.h"
#include "containers/object_pool.h"
#include "ipc/messages.h"
#include "output/text_terminal.h"
#include "processes/process.h"

namespace ipc {

using containers::ObjectPool;
using containers::RecursiveInterruptSafeSpinlockGuard;
using processes::GetProcessFromPid;
using processes::GetProcessOrNextFromPid;
using processes::Process;
using processes::ProcessRef;

namespace {

// Checks if two service names match.
bool DoServiceNamesMatch(const char* a, const char* b) {
  return common::WordsEqual(a, b, kServiceNameWords);
}

// Maximum number of services that can be registered by a single process.
constexpr size_t kMaxServicesPerProcess = 128;

// Maximum number of service notifications that can be registered by a single process.
constexpr size_t kMaxServiceNotificationsPerProcess = 256;

// Global singleton instance of ServiceDirectory.
ServiceDirectory g_service_directory;

}  // namespace

ServiceDirectory& ServiceDirectory::Get() {
  return g_service_directory;
}

ServiceDirectory::ServiceDirectory() {}

void ServiceDirectory::Initialize() {
  lock_.Initialize();
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  new (&processes_to_be_notified_when_a_service_appears_)
      containers::LinkedList<
          ProcessToNotifyWhenServiceAppears,
          &ProcessToNotifyWhenServiceAppears::node_in_all_notifiations>();
}

void ServiceDirectory::Register(char* service_name, Process* process,
                                size_t message_id) {
  auto service = ObjectPool<Service>::Allocate();
  if (service == nullptr) return;

  service->process = process;
  service->message_id = message_id;
  for (int i = 0; i < kServiceNameWords; i++)
    ((size_t*)service->name)[i] = ((size_t*)service_name)[i];
  // 'name' reserves one byte beyond kServiceNameLength for the terminator, so
  // a full length name is preserved.
  service->name[kServiceNameLength] = '\0';

  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (process->service_count >= kMaxServicesPerProcess) {
    ObjectPool<Service>::Release(service);
    return;
  }
  if (process->services.SearchForItemEqualToValue(message_id) != nullptr) {
    ObjectPool<Service>::Release(service);
    return;
  }
  process->services.Insert(service);
  process->service_count++;

  for (ProcessToNotifyWhenServiceAppears* notification :
       processes_to_be_notified_when_a_service_appears_) {
    if (DoServiceNamesMatch(service_name, notification->service_name)) {
      SendKernelMessageToProcess(notification->process, notification->message_id,
                                 process->pid, message_id, 0, 0, 0);
    }
  }
}

void ServiceDirectory::UnregisterByMessageId(Process* process,
                                             size_t message_id) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  Service* service = process->services.SearchForItemEqualToValue(message_id);
  if (service != nullptr) Unregister(service);
}

void ServiceDirectory::Unregister(Service* service) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  while (!service->processes_to_notify_on_disappear.IsEmpty()) {
    auto* notification = service->processes_to_notify_on_disappear.FirstItem();
    notification->process->services_i_want_to_be_notified_of_when_they_disappear
        .Remove(notification);
    SendKernelMessageToProcess(notification->process, notification->message_id,
                               0, 0, 0, 0, 0);
    StopNotifyingWhenDisappears(notification);
  }

  service->process->services.Remove(service);
  service->process->service_count--;
  ObjectPool<Service>::Release(service);
}


Service* ServiceDirectory::FindByProcessAndMid(size_t pid, size_t message_id) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  ProcessRef process = GetProcessFromPid(pid);
  if (!process) return nullptr;
  return process->services.SearchForItemEqualToValue(message_id);
}

size_t ServiceDirectory::QueryServices(const char* service_name, size_t min_pid,
                                       size_t min_message_id, size_t* pids,
                                       size_t* sids, size_t max_results) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  size_t services_found = 0;
  for (ProcessRef process = GetProcessOrNextFromPid(min_pid); process;
       process = GetProcessOrNextFromPid(process->pid + 1)) {
    Service* service = (process->pid == min_pid)
        ? process->services.SearchForItemGreaterThanOrEqualToValue(min_message_id)
        : process->services.FirstItem();
    while (service != nullptr) {
      if (DoServiceNamesMatch(service_name, service->name)) {
        if (services_found < max_results) {
          pids[services_found] = process->pid;
          sids[services_found] = service->message_id;
        }
        services_found++;
      }
      service = process->services.NextItem(service);
    }
  }
  return services_found;
}

bool ServiceDirectory::GetServiceName(size_t pid, size_t message_id,
                                      char* name_out) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  ProcessRef process = GetProcessFromPid(pid);
  if (!process) return false;
  Service* service = process->services.SearchForItemEqualToValue(message_id);
  if (service == nullptr) return false;
  memcpy(name_out, service->name, kServiceNameLength);
  return true;
}

Service* ServiceDirectory::FindNextByPidAndMidWithName(char* service_name,
                                                       size_t min_pid,
                                                       size_t min_message_id) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  for (ProcessRef process = GetProcessOrNextFromPid(min_pid); process;
       process = GetProcessOrNextFromPid(process->pid + 1)) {
    Service* service = (process->pid == min_pid)
        ? process->services.SearchForItemGreaterThanOrEqualToValue(min_message_id)
        : process->services.FirstItem();
    while (service != nullptr) {
      if (DoServiceNamesMatch(service_name, service->name)) return service;
      service = process->services.NextItem(service);
    }
  }
  return nullptr;
}

Service* ServiceDirectory::FindNextWithName(char* service_name,
                                            Service* previous_service) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  Process* start_proc = previous_service->process;
  Service* service = start_proc->services.NextItem(previous_service);
  while (service != nullptr) {
    if (DoServiceNamesMatch(service_name, service->name)) return service;
    service = start_proc->services.NextItem(service);
  }
  for (ProcessRef process = GetProcessOrNextFromPid(start_proc->pid + 1); process;
       process = GetProcessOrNextFromPid(process->pid + 1)) {
    service = process->services.FirstItem();
    while (service != nullptr) {
      if (DoServiceNamesMatch(service_name, service->name)) return service;
      service = process->services.NextItem(service);
    }
  }
  return nullptr;
}

void ServiceDirectory::NotifyWhenAppears(char* service_name, Process* process,
                                         size_t message_id) {
  auto notification = ObjectPool<ProcessToNotifyWhenServiceAppears>::Allocate();
  if (notification == nullptr) return;

  notification->process = process;
  notification->message_id = message_id;
  for (int i = 0; i < kServiceNameWords; i++)
    ((size_t*)notification->service_name)[i] = ((size_t*)service_name)[i];

  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  size_t count = 0;
  for (auto* existing :
       process->services_i_want_to_be_notified_of_when_they_appear) {
    if (existing->message_id == message_id &&
        DoServiceNamesMatch(service_name, existing->service_name)) {
      ObjectPool<ProcessToNotifyWhenServiceAppears>::Release(notification);
      return;
    }
    count++;
  }
  if (count >= kMaxServiceNotificationsPerProcess) {
    ObjectPool<ProcessToNotifyWhenServiceAppears>::Release(notification);
    return;
  }

  processes_to_be_notified_when_a_service_appears_.AddBack(notification);
  process->services_i_want_to_be_notified_of_when_they_appear.AddBack(
      notification);

  for (ProcessRef process_to_scan = GetProcessOrNextFromPid(0); process_to_scan;
       process_to_scan = GetProcessOrNextFromPid(process_to_scan->pid + 1)) {
    for (Service* service : process_to_scan->services) {
      if (DoServiceNamesMatch(service_name, service->name)) {
        SendKernelMessageToProcess(process, message_id, service->process->pid,
                                   service->message_id, 0, 0, 0);
      }
    }
  }
}

void ServiceDirectory::StopNotifyingWhenAppearsByMessageId(Process* process,
                                                          size_t message_id) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  for (auto* notification =
           process->services_i_want_to_be_notified_of_when_they_appear
               .FirstItem();
       notification != nullptr;) {
    auto* next_notification =
        process->services_i_want_to_be_notified_of_when_they_appear.NextItem(
            notification);
    if (notification->message_id == message_id)
      StopNotifyingWhenAppears(notification);
    notification = next_notification;
  }
}

void ServiceDirectory::StopNotifyingWhenAppears(
    ProcessToNotifyWhenServiceAppears* notification) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  processes_to_be_notified_when_a_service_appears_.Remove(notification);
  notification->process->services_i_want_to_be_notified_of_when_they_appear
      .Remove(notification);
  ObjectPool<ProcessToNotifyWhenServiceAppears>::Release(notification);
}

void ServiceDirectory::NotifyWhenDisappears(Process* process,
                                            size_t service_process_id,
                                            size_t service_message_id,
                                            size_t message_id) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  auto* service =
      FindByProcessAndMid(service_process_id, service_message_id);

  if (service != nullptr) {
    size_t count = 0;
    for (auto* existing :
         process->services_i_want_to_be_notified_of_when_they_disappear) {
      if (existing->service == service && existing->message_id == message_id) {
        return;
      }
      count++;
    }
    if (count >= kMaxServiceNotificationsPerProcess) return;

    auto notification =
        ObjectPool<ProcessToNotifyWhenServiceDisappears>::Allocate();
    if (notification == nullptr) return;

    notification->process = process;
    notification->service = service;
    notification->message_id = message_id;

    process->services_i_want_to_be_notified_of_when_they_disappear.AddBack(
        notification);
    service->processes_to_notify_on_disappear.AddBack(notification);
  } else {
    SendKernelMessageToProcess(process, message_id, 0, 0, 0, 0, 0);
  }
}

void ServiceDirectory::StopNotifyingWhenDisappears(Process* process,
                                                  size_t message_id) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  for (auto* notification =
           process->services_i_want_to_be_notified_of_when_they_disappear
               .FirstItem();
       notification != nullptr;) {
    auto* next_notification =
        process->services_i_want_to_be_notified_of_when_they_disappear.NextItem(
            notification);
    if (notification->message_id == message_id)
      StopNotifyingWhenDisappears(notification);
    notification = next_notification;
  }
}

void ServiceDirectory::StopNotifyingWhenDisappears(
    ProcessToNotifyWhenServiceDisappears* notification) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  notification->service->processes_to_notify_on_disappear.Remove(notification);
  notification->process->services_i_want_to_be_notified_of_when_they_disappear
      .Remove(notification);
  ObjectPool<ProcessToNotifyWhenServiceDisappears>::Release(notification);
}

}  // namespace ipc
