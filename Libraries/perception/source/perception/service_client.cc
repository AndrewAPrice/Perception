// Copyright 2025 Google LLC
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

#include "perception/service_client.h"

#include "perception/serialization/serializer.h"
#include "perception/services.h"

namespace perception {

ServiceClient::ServiceClient(ProcessId process_id, MessageId message_id)
    : process_id_(process_id), message_id_(message_id) {}

void ServiceClient::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Process ID", process_id_);
  serializer.Integer("Message ID", message_id_);
}

ProcessId ServiceClient::ServerProcessId() const { return process_id_; }

MessageId ServiceClient::ServiceId() const { return message_id_; }

bool ServiceClient::IsValid() const { return process_id_ != 0; }

MessageId ServiceClient::NotifyOnDisappearence(
    const std::function<void()>& on_disappearance) {
  return NotifyWhenServiceDisappears(process_id_, message_id_,
                                     on_disappearance);
}

void ServiceClient::StopNotifyingOnDisappearance(MessageId message_id) {
  ::perception::StopNotifyWhenServiceDisappears(message_id);
}

}  // namespace perception
