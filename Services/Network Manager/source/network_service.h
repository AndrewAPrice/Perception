// Copyright 2026 Google LLC
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

#include <stddef.h>

#include <map>
#include <memory>

#include "perception/network/network_service.h"

class SocketImpl;

class NetworkService : public ::perception::network::NetworkService::Server {
 public:
  NetworkService();

  virtual StatusOr<::perception::network::CreateSocketResponse> CreateSocket(
      const ::perception::network::CreateSocketRequest& request) override;

  virtual StatusOr<::perception::network::ResolveHostResponse> ResolveHost(
      const ::perception::network::ResolveHostRequest& request) override;

 private:
  std::map<size_t, std::shared_ptr<SocketImpl>> sockets_;
};
