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
#include <memory>
#include <string>
#include <string_view>

#include "perception/messages.h"
#include "perception/processes.h"
#include "perception/rpc_memory.h"
#include "perception/serialization/memory_read_stream.h"
#include "perception/serialization/serializable.h"
#include "perception/serialization/shared_memory_write_stream.h"
#include "perception/services.h"
#include "perception/tracing.h"
#include "status.h"

namespace perception {

class ServiceClient : public serialization::Serializable {
 public:
  ServiceClient(ProcessId process_id, MessageId message_id);
  virtual ~ServiceClient() {}
  virtual void Serialize(serialization::Serializer& serializer) override;

  template <class ResponseType, class RequestType>
  ResponseType SyncDispatch(const RequestType& request, size_t method_id,
                            std::string_view service_name = "",
                            std::string_view method_name = "") {
    std::string full_rpc_name;
    if (!service_name.empty() && !method_name.empty()) {
      full_rpc_name =
          std::string(service_name) + "." + std::string(method_name);
    }
    PERCEPTION_TRACE_SPAN_CAT(
        full_rpc_name.empty() ? "RPC.SyncDispatch" : full_rpc_name.c_str(),
        "rpc_out");

    MessageData message = {};
    if (!PrepareRequestMessageWithParameter<RequestType>(request, method_id,
                                                         message)) {
      return ResponseType(Status::OUT_OF_MEMORY);
    }
    return SyncDispatch<ResponseType>(message);
  }

  template <class ResponseType, class RequestType>
  ResponseType SyncDispatch(size_t method_id,
                            std::string_view service_name = "",
                            std::string_view method_name = "") {
    std::string full_rpc_name;
    if (!service_name.empty() && !method_name.empty()) {
      full_rpc_name =
          std::string(service_name) + "." + std::string(method_name);
    }
    PERCEPTION_TRACE_SPAN_CAT(
        full_rpc_name.empty() ? "RPC.SyncDispatch" : full_rpc_name.c_str(),
        "rpc_out");

    MessageData message = {};
    PrepareRequestMessageWithoutParameters(method_id, message);
    return SyncDispatch<ResponseType>(message);
  }

  template <class ResponseType, class RequestType>
  void AsyncDispatch(const RequestType& request, size_t method_id,
                     std::function<void(ResponseType)> on_response,
                     std::string_view service_name = "",
                     std::string_view method_name = "") {
    std::string full_rpc_name;
    if (!service_name.empty() && !method_name.empty()) {
      full_rpc_name =
          std::string(service_name) + "." + std::string(method_name);
    }
    const char* rpc_name_ptr =
        full_rpc_name.empty() ? "RPC.AsyncDispatch" : full_rpc_name.c_str();

    if (on_response) {
#ifdef ENABLE_TRACING
      auto trace_span =
          std::make_shared<AsyncTraceSpan>(rpc_name_ptr, "rpc_out");
#else
      std::shared_ptr<AsyncTraceSpan> trace_span = nullptr;
#endif
      MessageData message = {};
      if (!PrepareRequestMessageWithParameter<RequestType>(request, method_id,
                                                           message)) {
#ifdef ENABLE_TRACING
        if (trace_span) trace_span->End();
#endif
        Defer([on_response]() {
          on_response(ResponseType(Status::OUT_OF_MEMORY));
        });
        return;
      }
      AsyncDispatch<ResponseType>(message, on_response, trace_span);
    } else {
      PERCEPTION_TRACE_EVENT_CAT(rpc_name_ptr, "rpc_out");
      MessageData message = {};
      (void)PrepareRequestMessageWithParameter<RequestType>(request, method_id,
                                                            message);
      AsyncDispatch<ResponseType>(message, on_response, nullptr);
    }
  }

  template <class ResponseType, class RequestType>
  void AsyncDispatch(size_t method_id,
                     std::function<void(ResponseType)> on_response,
                     std::string_view service_name = "",
                     std::string_view method_name = "") {
    std::string full_rpc_name;
    if (!service_name.empty() && !method_name.empty()) {
      full_rpc_name =
          std::string(service_name) + "." + std::string(method_name);
    }
    const char* rpc_name_ptr =
        full_rpc_name.empty() ? "RPC.AsyncDispatch" : full_rpc_name.c_str();

    if (on_response) {
#ifdef ENABLE_TRACING
      auto trace_span =
          std::make_shared<AsyncTraceSpan>(rpc_name_ptr, "rpc_out");
#else
      std::shared_ptr<AsyncTraceSpan> trace_span = nullptr;
#endif
      MessageData message = {};
      PrepareRequestMessageWithoutParameters(method_id, message);
      AsyncDispatch<ResponseType>(message, on_response, trace_span);
    } else {
      PERCEPTION_TRACE_EVENT_CAT(rpc_name_ptr, "rpc_out");
      MessageData message = {};
      PrepareRequestMessageWithoutParameters(method_id, message);
      AsyncDispatch<ResponseType>(message, on_response, nullptr);
    }
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
    message.param1 = message_id_of_response;
    SetMessageType(message.metadata, MessageType::CALL);

    RegisterWakeUpHandler(message_id_of_response);

    auto send_status = SendMessage(process_id_, message);
    if (send_status != Status::OK) {
      UnregisterMessageHandler(message_id_of_response);
      if (message.param3 != SIZE_MAX) {
        auto shared_memory =
            GetMemoryBufferForSendingToProcessRegardlessOfIfInUse(
                process_id_, message.param3);
        if (shared_memory)
          SetMemoryBufferAsReadyForSendingNextMessageToProcess(*shared_memory);
      }

      return ::perception::ToStatus(send_status);
    }

    // Sleep until there is a response.
    ProcessId pid;
    while (true) {
      SleepAndGetRawMessage(message_id_of_response, pid, message);
      if (pid == process_id_) break;
      DealWithUnhandledMessage(pid, message);
      // Re-register if it wasn't the expected sender.
      RegisterWakeUpHandler(message_id_of_response);
    }

    return LoadResponseFromMessageData<ResponseType>(pid, message);
  }

  template <class ResponseType>
  void AsyncDispatch(
      MessageData& message, std::function<void(ResponseType)> on_response,
      [[maybe_unused]] std::shared_ptr<AsyncTraceSpan> trace_span = nullptr) {
    if (on_response) {
      // Care about waiting for a response.
      MessageId message_id_of_response = GenerateUniqueMessageId();
      message.param1 = message_id_of_response;
      SetMessageType(message.metadata, MessageType::CALL);

      auto send_status = SendMessage(process_id_, message);
      if (send_status != Status::OK) {
        if (message.param3 != SIZE_MAX) {
          auto shared_memory =
              GetMemoryBufferForSendingToProcessRegardlessOfIfInUse(
                  process_id_, message.param3);
          if (shared_memory)
            SetMemoryBufferAsReadyForSendingNextMessageToProcess(
                *shared_memory);
        }

#ifdef ENABLE_TRACING
        if (trace_span) trace_span->End();
#endif
        // Something went wrong while sending it out.
        Defer([on_response, send_status]() {
          on_response(ToStatus(send_status));
        });
        return;
      }

      RegisterMessageHandler(
          message_id_of_response,
          [expected_sender = process_id_, message_id_of_response, on_response
#ifdef ENABLE_TRACING
           ,
           trace_span
#endif
      ](ProcessId sender, const MessageData& message) {
            if (sender != expected_sender) {
              DealWithUnhandledMessage(sender, message);
              return;  // Not the correct process.
            }

            UnregisterMessageHandler(message_id_of_response);

            ResponseType response =
                LoadResponseFromMessageData<ResponseType>(sender, message);
            on_response(std::move(response));

#ifdef ENABLE_TRACING
            if (trace_span) trace_span->End();
#endif
          });
    } else {
      // Don't care about waiting for a response.
      SetMessageType(message.metadata, MessageType::ONE_WAY);
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
          if (shared_memory->Grow(message.param3)) {
            serialization::DeserializeFromSharedMemory(*response, *shared_memory, 1,
                                        message.param4);
            SetMemoryBufferAsReadyForSendingNextMessageToProcess(*shared_memory);
          }
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
  bool PrepareRequestMessageWithParameter(const RequestType& request,
                                          size_t method_id,
                                          MessageData& message) {
    PrepareRequestMessage(method_id, message);

    auto shared_memory = GetMemoryBufferForSendingToProcess(process_id_);
    if (!shared_memory) return false;

    message.param5 =
        serialization::SerializeToSharedMemory(request, *shared_memory, 1);
    message.param3 = shared_memory->GetId();
    message.param4 = shared_memory->GetSize();
    return true;
  }

  ProcessId process_id_;
  MessageId message_id_;
};

}  // namespace perception