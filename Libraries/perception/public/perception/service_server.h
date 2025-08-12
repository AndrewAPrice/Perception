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

#pragma once

// Define VERBOSE for printing failures.
#define VERBOSE

#include <functional>
#include <string_view>

#ifdef VERBOSE
#include <iostream>

#include "perception/processes.h"
#include "perception/serialization/text_serializer.h"
#endif

#include "perception/messages.h"
#include "perception/rpc_memory.h"
#include "perception/serialization/memory_read_stream.h"
#include "perception/serialization/shared_memory_write_stream.h"
#include "perception/shared_memory.h"
#include "status.h"

namespace perception {

struct ServiceServerOptions {};

class ServiceServer {
 public:
  ProcessId ServerProcessId() const;

  MessageId ServiceId() const;

 protected:
  ServiceServer(ServiceServerOptions options, std::string_view service_name);

  virtual ~ServiceServer();

  template <class ResponseType, class RequestType, class Service>
  void HandleExpectedRequest(
      Service* service,
      ResponseType (Service::*handler)(const RequestType&, ProcessId),
      ProcessId sender, const MessageData& message) {
    RequestType request;

    if (message.param3 == SIZE_MAX) {
      // No attached message.
      serialization::DeserializeToEmpty(request);
    } else {
      // Deserialized attached memory.
      auto shared_memory =
          GetMemoryBufferForReceivingFromProcess(sender, message.param3);
      shared_memory->Grow(message.param4);
      serialization::DeserializeFromSharedMemory(request, *shared_memory, 1);

      SetMemoryBufferAsReadyForSendingNextMessageToProcess(*shared_memory);
    }

    if (message.param2 == SIZE_MAX) {
      // Don't care about a response.
      (void)(service->*handler)(request, sender);
      return;
    }
    auto response = (service->*handler)(request, sender);
    Status status = ToStatus(response);
    if (status != Status::OK) {
      std::cout << "Bad status " << (int)status << " from "
                << Service::FullyQualifiedName() << "(\""
                << GetProcessName() << "\") to \"" << GetProcessName(sender)
                << "\" with request: " << std::endl
                << SerializeToString(request) << std::endl;
    }
#endif

    SendBackResponse(response, sender, message);
  }

  template <class ResponseType, class RequestType, class Service>
  void HandleExpectedRequest(Service* service,
                             ResponseType (Service::*handler)(ProcessId),
                             ProcessId sender, const MessageData& message) {
    HandleUnexpectedMessageInRequest(sender, message);

    if (message.param2 == SIZE_MAX) {
      // Don't care about a response.
      (void)(service->*handler)(sender);
      return;
    }

    auto response = (service->*handler)(sender);
    SendBackResponse(response, sender, message);
  }

  void HandleUnknownRequest(ProcessId sender, const MessageData& params);

  virtual void HandleRequest(ProcessId sender, const MessageData& params) = 0;

 private:
  template <class ResponseType>
  void SendBackResponse(ResponseType& response, ProcessId sender,
                        const MessageData& message) {
    MessageData response_data;
    response_data.message_id = message.param2;
    response_data.metadata = 0;
    response_data.param1 =
        static_cast<size_t>(::perception::ToStatus(response));
    if constexpr (std::is_same_v<ResponseType, Status>) {
      // Send back just the status.
      response_data.param2 = SIZE_MAX;
      response_data.param3 = 0;
    } else {
      if (response.Ok()) {
        // Send back response type.
        auto shared_memory = GetMemoryBufferForSendingToProcess(sender);
        serialization::SerializeToSharedMemory(*response, *shared_memory, 1);
        response_data.param2 = shared_memory->GetId();
        response_data.param3 = shared_memory->GetSize();
      } else {
        // Send back non-ok status.
        response_data.param2 = SIZE_MAX;
        response_data.param3 = 0;
        // TODO: Could send a status string in the remaining param bytes.
      }
    }
    SendMessage(sender, response_data);
  }

  void HandleUnexpectedMessageInRequest(ProcessId sender,
                                        const MessageData& message);

  ServiceServerOptions options_;
  MessageId message_id_;
};

}  // namespace perception
