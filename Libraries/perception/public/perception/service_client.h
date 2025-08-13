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
#include "perception/rpc_memory.h"
#include "perception/serialization/memory_read_stream.h"
#include "perception/serialization/serializable.h"
#include "perception/serialization/shared_memory_write_stream.h"
#include "perception/services.h"
#include "status.h"

namespace perception {

class ServiceClient : public serialization::Serializable {
 public:
  ServiceClient(ProcessId process_id, MessageId message_id);
  virtual ~ServiceClient() {}
  virtual void Serialize(serialization::Serializer& serializer) override;

  template <class ResponseType, class RequestType>
  ResponseType SyncDispatch(const RequestType& request, size_t method_id) {
    MessageData message;
    PrepareRequestMessageWithParameter<RequestType>(request, method_id,
                                                    message);
    return SyncDispatch<ResponseType>(message);
  }

  template <class ResponseType, class RequestType>
  ResponseType SyncDispatch(size_t method_id) {
    MessageData message;
    PrepareRequestMessageWithoutParameters(method_id, message);
    return SyncDispatch<ResponseType>(message);
  }

  template <class ResponseType, class RequestType>
  void AsyncDispatch(const RequestType& request, size_t method_id,
                     std::function<void(ResponseType)> on_response) {
    MessageData message;
    PrepareRequestMessageWithParameter<RequestType>(request, method_id,
                                                    message);
    AsyncDispatch<ResponseType>(message, on_response);
  }

  template <class ResponseType, class RequestType>
  void AsyncDispatch(size_t method_id,
                     std::function<void(ResponseType)> on_response) {
    MessageData message;
    PrepareRequestMessageWithoutParameters(method_id, message);

    AsyncDispatch<ResponseType>(message, on_response);
  }

  ProcessId ServerProcessId() const;

  MessageId ServiceId() const;

  bool operator<(const ServiceClient& rhs) const;

  bool IsValid() const;

  operator bool() const { return IsValid(); }

  MessageId NotifyOnDisappearance(
      const std::function<void()>& on_disappearance);
  void StopNotifyingOnDisappearance(MessageId message_id);

 protected:
  template <class ResponseType>
  ResponseType SyncDispatch(MessageData& message) {
    MessageId message_id_of_response = GenerateUniqueMessageId();
    message.param2 = message_id_of_response;

    auto send_status = SendMessage(process_id_, message);
    if (send_status != MessageStatus::SUCCESS) {
      if (message.param3 != SIZE_MAX) {
        auto shared_memory =
            GetMemoryBufferForSendingToProcessRegardlessOfIfInUse(process_id_);
        if (shared_memory)
          SetMemoryBufferAsReadyForSendingNextMessageToProcess(*shared_memory);
      }

      return ::perception::ToStatus(send_status);
    }

    // Sleep until there is a response.
    ProcessId pid;
    do {
      SleepUntilMessage(message_id_of_response, pid, message);
    } while (pid != process_id_);

    return LoadResponseFromMessageData<ResponseType>(pid, message);
  }

  template <class ResponseType>
  void AsyncDispatch(MessageData& message,
                     std::function<void(ResponseType)> on_response) {
    if (on_response) {
      // Care about waiting for a response.
      MessageId message_id_of_response = GenerateUniqueMessageId();
      message.param2 = message_id_of_response;

      auto send_status = SendMessage(process_id_, message);
      if (send_status != MessageStatus::SUCCESS) {
        if (message.param3 != SIZE_MAX) {
          auto shared_memory =
              GetMemoryBufferForSendingToProcessRegardlessOfIfInUse(
                  process_id_);
          if (shared_memory)
            SetMemoryBufferAsReadyForSendingNextMessageToProcess(
                *shared_memory);
        }

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

            ResponseType response =
                LoadResponseFromMessageData<ResponseType>(sender, message);
            on_response(response);
          });
    } else {
      // Don't care about waiting for a response.
      message.param2 = SIZE_MAX;
      (void)SendMessage(process_id_, message);
    }
  }

  template <class ResponseType>
  static ResponseType LoadResponseFromMessageData(ProcessId process_id,
                                                  const MessageData& message) {
    ResponseType response;
    auto status = static_cast<Status>(message.param1);
    response = status;
    if constexpr (std::is_same_v<ResponseType, Status>) {
      // Just care about the status.
      MaybeHandleUnexpectedMemoryInResponse(process_id, message);
    } else {
      if (status == Status::OK) {
        if (message.param2 == SIZE_MAX) {
          serialization::DeserializeToEmpty(*response);
        } else {
          auto shared_memory = GetMemoryBufferForReceivingFromProcess(
              process_id, message.param2);
          shared_memory->Grow(message.param3);
          DeserializeFromSharedMemory(*response, *shared_memory, 1);
          SetMemoryBufferAsReadyForSendingNextMessageToProcess(*shared_memory);
        }
      } else {
        MaybeHandleUnexpectedMemoryInResponse(process_id, message);
      }
    }
    return response;
  }

  static void MaybeHandleUnexpectedMemoryInResponse(ProcessId process_id,
                                                    const MessageData& message);

  void PrepareRequestMessage(size_t method_id, MessageData& message);

  void PrepareRequestMessageWithoutParameters(size_t method_id,
                                              MessageData& message);

  template <class RequestType>
  void PrepareRequestMessageWithParameter(const RequestType& request,
                                          size_t method_id,
                                          MessageData& message) {
    PrepareRequestMessage(method_id, message);

    auto shared_memory = GetMemoryBufferForSendingToProcess(process_id_);
    serialization::SerializeToSharedMemory(request, *shared_memory, 1);
    message.param3 = shared_memory->GetId();
    message.param4 = shared_memory->GetSize();
  }

  ProcessId process_id_;
  MessageId message_id_;
};

}  // namespace perception