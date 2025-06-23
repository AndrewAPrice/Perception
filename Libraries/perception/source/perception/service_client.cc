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
    : process_id_(process_id), message_id_(message_id) {
}

void ServiceClient::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Process ID", process_id_);
  serializer.Integer("Message ID", message_id_);
}

ProcessId ServiceClient::ServerProcessId() const { return process_id_; }

MessageId ServiceClient::ServiceId() const { return message_id_; }

bool ServiceClient::IsValid() const { return process_id_ != 0; }

MessageId ServiceClient::NotifyOnDisappearance(
    const std::function<void()>& on_disappearance) {
  return NotifyWhenServiceDisappears(process_id_, message_id_,
                                     on_disappearance);
}

void ServiceClient::StopNotifyingOnDisappearance(MessageId message_id) {
  ::perception::StopNotifyWhenServiceDisappears(message_id);
}

void ServiceClient::MaybeHandleUnexpectedMemoryInResponse(
    ProcessId process_id, const MessageData& message) {
  if (message.param2 == SIZE_MAX) return;

  // If there is a message attached, but it's not needed, so clear the
  // status bit.
  auto shared_memory =
      GetMemoryBufferForReceivingFromProcess(process_id, message.param2);
  SetMemoryBufferAsReadyForSendingNextMessageToProcess(*shared_memory);
}

void ServiceClient::PrepareRequestMessage(size_t method_id,
                                          MessageData& message) {
  message.message_id = message_id_;
  message.metadata = 0;
  message.param1 = method_id;
}

void ServiceClient::PrepareRequestMessageWithoutParameters(
    size_t method_id, MessageData& message) {
  PrepareRequestMessage(method_id, message);
  message.param3 = SIZE_MAX;
}

}  // namespace perception
