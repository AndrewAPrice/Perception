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

#include "perception/service_server.h"

#include <string_view>

#include "perception/messages.h"
#include "perception/processes.h"
#include "perception/rpc_memory.h"
#include "perception/services.h"

namespace perception {

// Implementation details:
// Request:
//    metadata:
//    param 1: method ID
//    param 2: message ID to use for response, or MAX if caller doesn't want a
//    response
//    param 3: shared buffer ID, or MAX for no attached messag
//    param 4: length of message, in bytes (page aligned)
//    param 5:
//
// Response:
//    metadata:
//    param 1: status
//    param 2: shared buffer ID of response, or MAX for no attached method
//    param 3: length of message, in byes (page aligned)
//    param 4:
//    param 5:
//

ServiceServer::ServiceServer(ServiceServerOptions options,
                             std::string_view service_name)
    : options_(options) {
  message_id_ = GenerateUniqueMessageId();
  RegisterRawMessageHandler(
      message_id_, [this](::perception::ProcessId sender,
                          const ::perception::MessageData& message_data) {
        this->HandleRequest(sender, message_data);
      });
  RegisterService(message_id_, service_name);
}

ServiceServer::~ServiceServer() {
  UnregisterService(message_id_);
  UnregisterMessageHandler(message_id_);
}

void ServiceServer::HandleUnknownRequest(ProcessId sender,
                                         const MessageData& message) {
  HandleUnexpectedMessageInRequest(sender, message);
  if (message.param2 == SIZE_MAX) return;  // Dont care about a response.

  // Send back an unimplemented status.
  MessageData response_data;
  response_data.message_id = message.param2;
  response_data.metadata = 0;
  response_data.param1 = static_cast<size_t>(Status::UNIMPLEMENTED);
  response_data.param2 = SIZE_MAX;
  response_data.param3 = 0;
  SendMessage(sender, response_data);
}

ProcessId ServiceServer::ServerProcessId() const { GetProcessId(); }

MessageId ServiceServer::ServiceId() const { return message_id_; }

void ServiceServer::HandleUnexpectedMessageInRequest(
    ProcessId sender, const MessageData& message) {
  if (message.param3 == SIZE_MAX) return;
  auto shared_memory =
      GetMemoryBufferForReceivingFromProcess(sender, message.param3);
  SetMemoryBufferAsReadyForSendingNextMessageToProcess(*shared_memory);
}

}  // namespace perception