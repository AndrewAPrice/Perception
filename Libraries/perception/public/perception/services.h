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

#include "types.h"

#include <functional>
#include <string_view>

namespace perception {

// Register a service so that others can find it.
void RegisterService(MessageId message_id, std::string_view name);

// Unregister a service and notifies anyone interested that we no longer exist.
void UnregisterService(MessageId message_id);

// Finds the first service with a given name. Returns if there is at least one
// service.
bool FindFirstInstanceOfService(std::string_view name, ProcessId& process,
	MessageId& message);

// Calls the handler for each instance of the service.
void ForEachInstanceOfService(std::string_view name,
	const std::function<void(ProcessId, MessageId)>& on_each_service);

// Calls the handler for each instance of the service that currently exists,
// and every time a new instance is registered.
MessageId NotifyOnEachNewServiceInstance(std::string_view name,
	const std::function<void(ProcessId, MessageId)>& on_each_service);

// Stops calling the handler passed to NotifyOnEachService each time a new
// instance of the service is registered.
void StopNotifyingOnEachNewServiceInstance(MessageId message_id);

// Calls the handler when the service disappears. The handler automatically
// unregisters if it gets triggered (although it's safe to accidentally
// call StopNotifyWhenServiceDisappears if it has triggered.)
MessageId NotifyWhenServiceDisappears(ProcessId process_id,
	MessageId message_id, const std::function<void()>& on_disappearance);

// No longer calls the handler when the service disappears.
void StopNotifyWhenServiceDisappears(MessageId message_id);

}
