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

#include "network_service.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include "dns.h"
#include "endian.h"
#include "interface.h"
#include "perception/fibers.h"
#include "perception/network/socket.h"
#include "perception/time.h"
#include "protocols.h"
#include "socket.h"

using ::perception::network::CreateSocketRequest;
using ::perception::network::CreateSocketResponse;
using ::perception::network::IpAddress;
using ::perception::network::ResolveHostRequest;
using ::perception::network::ResolveHostResponse;
using ::perception::network::Socket;
using ::perception::network::SocketProtocol;

NetworkService::NetworkService()
    : ::perception::network::NetworkService::Server() {}

StatusOr<CreateSocketResponse> NetworkService::CreateSocket(
    const CreateSocketRequest& request) {
  SocketType type = (request.protocol == SocketProtocol::TCP) ? SocketType::TCP
                                                              : SocketType::UDP;
  auto socket = std::make_shared<SocketImpl>(type);

  CreateSocketResponse response;
  response.socket = Socket::Client(*socket);

  AddActiveSocket(socket);
  sockets_[socket->ServiceId()] = socket;
  return response;
}

StatusOr<ResolveHostResponse> NetworkService::ResolveHost(
    const ResolveHostRequest& request) {
  ResolveHostResponse response;

  if (GetNetworkInterfaceCount() == 0) return Status::INTERNAL_ERROR;

  int ip_parts[4];
  if (std::sscanf(request.host.c_str(), "%d.%d.%d.%d", &ip_parts[0],
                  &ip_parts[1], &ip_parts[2], &ip_parts[3]) == 4) {
    IpAddress addr;
    addr.address[0] = (uint8)ip_parts[0];
    addr.address[1] = (uint8)ip_parts[1];
    addr.address[2] = (uint8)ip_parts[2];
    addr.address[3] = (uint8)ip_parts[3];
    response.addresses.push_back(addr);
    return response;
  }

  return PerformDnsResolution(request.host);
}
