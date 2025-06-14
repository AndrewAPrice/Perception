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

#include <functional>
#include <string_view>

#include "perception/messages.h"
#include "perception/rpc_memory.h"

namespace perception {

struct RpcServerOptions {
  bool is_public;
};

class RpcServer {
 protected:
  RpcServer(RpcServerOptions options, std::string_view service_name);

  virtual ~RpcServer();

  template <class ResponseType, class RequestType, class Service>
  void HandleExpectedRequest(
      Service* service, ResponseType (Service::*handler)(const RequestType&),
      ProcessId sender, const MessageData& message) {
    RequestType request_type;
    (service->*handler)(request_type);
  }

  template <class ResponseType, class RequestType, class Service>
  void HandleExpectedRequest(Service* service,
                             ResponseType (Service::*handler)(),
                             ProcessId sender, const MessageData& message) {
    if (message.param1 != SIZE_MAX) {
      // Message sent a request, but not expecting a request. Clear the first
      // byte in the memory page.
      auto request_memory =
          GetMemoryBufferForReceivingFromProcess(sender, message.param1);
      request_memory->Apply([](void* ptr, size_t size) {
        if (size > 0) *(unsigned char*)ptr = 0;
      });
    }

    auto response = (service->*handler)();
  }

  void HandleUnknownRequest(ProcessId sender, const MessageData& params);

  virtual void HandleRequest(ProcessId sender, const MessageData& params) = 0;

 private:
  RpcServerOptions options_;
  MessageId message_id_;
};

}  // namespace perception
