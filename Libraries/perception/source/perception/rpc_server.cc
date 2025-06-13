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

#include "perception/rpc_server.h"

#include <string_view>

#include "perception/messages.h"
#include "perception/services.h"

namespace perception {

RpcServer::RpcServer(RpcServerOptions options, std::string_view service_name)
    : options_(options) {
  message_id_ = GenerateUniqueMessageId();
  RegisterRawMessageHandler(
      message_id_, [this](::perception::ProcessId sender,
                          const ::perception::MessageData& message_data) {
        this->HandleRequest(sender, message_data);
      });
  if (options.is_public) {
    RegisterService(message_id_, service_name);
  }
}

RpcServer::~RpcServer() {
  if (options_.is_public) {
    UnregisterService(message_id_);
  }
  UnregisterMessageHandler(message_id_);
}

void RpcServer::HandleUnknownRequest(ProcessId sender,
                                     const MessageData& params) {}

}  // namespace perception