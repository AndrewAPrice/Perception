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

#include <types.h>

#include <functional>

#include "perception/messages.h"
#include "perception/serialization/serializable.h"
#include "perception/services.h"

namespace perception {

class ServiceClient : public serialization::Serializable {
 public:
  ServiceClient(ProcessId process_id, MessageId message_id);
  virtual ~ServiceClient() {}
  virtual void Serialize(serialization::Serializer& serializer) override;

  template <class ResponseType, class RequestType>
  ResponseType SyncDispatch(RequestType& request_type, size_t method_id) {
    ResponseType response;
    return response;
  }

  template <class ResponseType, class RequestType>
  ResponseType SyncDispatch(size_t method_id) {
    ResponseType response;
    return response;
  }

  template <class ResponseType, class RequestType>
  void AsyncDispatch(RequestType& request_type, size_t method_id,
                     std::function<void(ResponseType)> on_response) {
    if (on_response) {
      // Care about waiting for a response.
    } else {
      // Don't care about waiting for a response.
    }
  }

  template <class ResponseType, class RequestType>
  void AsyncDispatch(size_t method_id,
                     std::function<void(ResponseType)> on_response) {
    MessageData message;
    message.message_id = message_id_;
    // TODO: Clean up having to shift.
    message.metadata = method_id << 3;
    message.param2 = SIZE_MAX;
    message.param3 = 0;
    if (on_response) {
      // Care about waiting for a response.
      ::perception::MessageId message_id_of_response =
          ::perception::GenerateUniqueMessageId();
      message.param1 = message_id_of_response;

      auto send_status = SendMessage(process_id_, message);
      if (!send_status.IsOk()) {
        // Something went wrong while sending it out.
        Defer([on_response, send_status]() {
          on_response(ToStatus(send_status));
        });
        return;
      }

      RegisterMessageHandler(
          message_id_of_response,
          [expected_sender = process_id_, message_id_of_response, on_response](
              ProcessId sender, const MessageData& message) {
            if (sender != expected_sender) return;  // Not the correct process.

            UnregisterMessageHandler(message_id_of_response);

            if (message_data.param1 != 0) {
              // Bad response from the server.
              return on_response(ToStatus(message_data.param1));
            }

            // Return the response.
            if constexpr (std::is_same_v<ResponseType, Status>) {
              HandleUnexpectedMessageInResponse(sender, message);
              // Just care about the status.
              on_response(ToStatus(message_data.param1));
            } else {
              ResponseType response;

              if (message.param3 == SIZE_MAX) {
                // No attached message.
                serialization::DeserializeToEmpty(*response);
              } else {
                auto shared_memory = GetMemoryBufferForReceivingFromProcess(
                    sender, message.param3);
                shared_memory->Grow(message.param4);
                serialization::DeserializeFromSharedMemory(*response,
                                                           *shared_memory, 1);
                SetMemoryBufferAsReadyForSendingNextMessageToProcess(
                    *shared_memory);
              }
              on_response(response);
            }
          });
    } else {
      // Don't care about waiting for a response.
      message.param1 = SIZE_MAX;
      (void)SendMessage(process_id_, message);
    }
  }

  ProcessId ServerProcessId() const;

  MessageId ServiceId() const;

  bool IsValid() const;

  MessageId NotifyOnDisappearence(
      const std::function<void()>& on_disappearance);
  void StopNotifyingOnDisappearance(MessageId message_id);

 protected:
  void HandleUnexpectedMessageInResponse(ProcessId sender,
                                         const MessageData& message);

  ProcessId process_id_;
  MessageId message_id_;
};

}  // namespace perception