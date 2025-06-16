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

#include <types.h>

#include <functional>
#include <mutex>
#include <string>
#include <string_view>

#include "perception/fibers.h"

namespace perception {

class ServiceClient;

// Calls the handler for each instance of the service that currently exists,
// and every time a new instance is registered.
MessageId NotifyOnEachNewServiceInstance(
    std::string_view name,
    const std::function<void(ProcessId, MessageId)>& on_each_service);

template <class ServiceType>
MessageId NotifyOnEachNewServiceInstance(
    const std::function<void(typename ServiceType::Client)>& on_each_service) {
  return NotifyOnEachNewServiceInstance(
      ServiceType::FullyQualifiedName(),
      [on_each_service](ProcessId process_id, MessageId message_id) {
        on_each_service(typename ServiceType::Client(process_id, message_id));
      });
}

// Stops calling the handler passed to NotifyOnEachService each time a new
// instance of the service is registered.
void StopNotifyingOnEachNewServiceInstance(MessageId message_id);

// Calls the handler when the service disappears. The handler automatically
// unregisters if it gets triggered (although it's safe to accidentally
// call StopNotifyWhenServiceDisappears if it has triggered.)
MessageId NotifyWhenServiceDisappears(
    ProcessId process_id, MessageId message_id,
    const std::function<void()>& on_disappearance);

MessageId NotifyWhenServiceDisappears(
    const ServiceClient& service_client,
    const std::function<void()>& on_disappearance);

// No longer calls the handler when the service disappears.
void StopNotifyWhenServiceDisappears(MessageId message_id);

// Register a service so that others can find it.
void RegisterService(MessageId message_id, std::string_view name);

// Unregister a service and notifies anyone interested that we no longer exist.
void UnregisterService(MessageId message_id);

// Finds the first service with a given name. Returns if there is at least one
// service.
bool FindFirstInstanceOfService(std::string_view name, ProcessId& process,
                                MessageId& message);

template <class ServiceType>
std::optional<typename ServiceType::Client> FindFirstInstanceOfService() {
  ProcessId process;
  MessageId message;
  if (FindFirstInstanceOfService(ServiceType::FullyQualifiedName(), process,
                                 message)) {
    return std::optional<typename ServiceType::Client>{
       typename ServiceType::Client(process, message)};
  } else {
    return std::nullopt;
  }
}

// Calls the handler for each instance of the service.
void ForEachInstanceOfService(
    std::string_view name,
    const std::function<void(ProcessId, MessageId)>& on_each_service);

template <class ServiceType>
static void ForEachInstanceOfService(
    const std::function<void(typename ServiceType::Client)>& on_each_instance) {
  ForEachInstanceOfService(
      ServiceType::FullyQualifiedName(),
      [on_each_instance](ProcessId process_id, MessageId message_id) {
        on_each_instance(typename ServiceType::Client(process_id, message_id));
      });
}

// Calls the handler for each registered service.
void ForEachService(
    const std::function<void(ProcessId, MessageId)>& on_each_service);

// Returns the name of a service.
std::string GetServiceName(ProcessId pid, MessageId message_id);

template <class ServiceType>
static typename ServiceType::Client GetService() {
  static typename ServiceType::Client singleton;
  static std::mutex mutex;

  std::scoped_lock lock(mutex);

  if (singleton.IsValid()) return singleton;

  auto main_fiber = ::perception::GetCurrentlyExecutingFiber();
  auto listening_message_id = NotifyOnEachNewInstance<ServiceType>(
      [main_fiber](typename ServiceType::Client instance) {
        if (!singleton.IsValid()) {
          singleton = instance;
          main_fiber->WakeUp();
        }
      });
  ::perception::Sleep();

  StopNotifyingOnEachNewServiceInstance(listening_message_id);

  (void)NotifyWhenServiceDisappears<ServiceType>(
      singleton, []() {
        std::scoped_lock lock(mutex);
        singleton = typename ServiceType::Client();
      });

  return singleton;
}

}  // namespace perception
