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

#include "ipc/service.h"

#include "ipc/service_directory.h"

namespace ipc {

void InitializeServices() {
  ServiceDirectory::Get().Initialize();
}

void RegisterService(char* service_name, processes::Process* process,
                     size_t message_id) {
  ServiceDirectory::Get().Register(service_name, process, message_id);
}

void UnregisterServiceByMessageId(processes::Process* process,
                                  size_t message_id) {
  ServiceDirectory::Get().UnregisterByMessageId(process, message_id);
}

void UnregisterService(Service* service) {
  ServiceDirectory::Get().Unregister(service);
}

Service* FindServiceByProcessAndMid(size_t pid, size_t message_id) {
  return ServiceDirectory::Get().FindByProcessAndMid(pid, message_id);
}

size_t QueryServices(const char* service_name, size_t min_pid,
                     size_t min_message_id, size_t* pids, size_t* sids,
                     size_t max_results) {
  return ServiceDirectory::Get().QueryServices(service_name, min_pid,
                                               min_message_id, pids, sids,
                                               max_results);
}

bool GetServiceName(size_t pid, size_t message_id, char* name_out) {
  return ServiceDirectory::Get().GetServiceName(pid, message_id, name_out);
}

Service* FindNextServiceByPidAndMidWithName(char* service_name, size_t min_pid,
                                            size_t min_message_id) {
  return ServiceDirectory::Get().FindNextByPidAndMidWithName(
      service_name, min_pid, min_message_id);
}

Service* FindNextServiceWithName(char* service_name,
                                 Service* previous_service) {
  return ServiceDirectory::Get().FindNextWithName(service_name,
                                                   previous_service);
}

void NotifyProcessWhenServiceAppears(char* service_name,
                                     processes::Process* process,
                                     size_t message_id) {
  ServiceDirectory::Get().NotifyWhenAppears(service_name, process, message_id);
}

void StopNotifyingProcessWhenServiceAppearsByMessageId(
    processes::Process* process, size_t message_id) {
  ServiceDirectory::Get().StopNotifyingWhenAppearsByMessageId(process,
                                                             message_id);
}

void StopNotifyingProcessWhenServiceAppears(
    ProcessToNotifyWhenServiceAppears* notification) {
  ServiceDirectory::Get().StopNotifyingWhenAppears(notification);
}

void NotifyProcessWhenServiceDisappears(processes::Process* process,
                                        size_t service_process_id,
                                        size_t service_message_id,
                                        size_t message_id) {
  ServiceDirectory::Get().NotifyWhenDisappears(
      process, service_process_id, service_message_id, message_id);
}

void StopNotifyingProcessWhenServiceDisappears(processes::Process* process,
                                               size_t message_id) {
  ServiceDirectory::Get().StopNotifyingWhenDisappears(process, message_id);
}

void StopNotifyingProcessWhenServiceDisappears(
    ProcessToNotifyWhenServiceDisappears* notification) {
  ServiceDirectory::Get().StopNotifyingWhenDisappears(notification);
}

}  // namespace ipc
