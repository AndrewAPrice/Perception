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
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "address_selection.h"
#include "dns.h"
#include "interface.h"
#include "perception/fibers.h"
#include "perception/network/socket.h"
#include "perception/time.h"
#include "socket.h"

using ::perception::network::ConnectRequest;
using ::perception::network::ConnectToHostRequest;
using ::perception::network::ConnectToHostResponse;
using ::perception::network::CreateSocketRequest;
using ::perception::network::CreateSocketResponse;
using ::perception::network::GetInterfacesResponse;
using ::perception::network::InterfaceAddressInfo;
using ::perception::network::InterfaceInfo;
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
  return PerformDnsResolution(request.host, request.family);
}
StatusOr<ConnectToHostResponse> NetworkService::ConnectToHost(
    const ConnectToHostRequest& request) {
  if (request.host.empty() || request.port == 0)
    return Status::INVALID_ARGUMENT;

  auto resolved = PerformDnsResolution(request.host, request.family);
  if (!resolved.Ok()) return resolved.Status();
  if (resolved->addresses.empty()) return Status::MISSING_MEDIA;

  std::vector<IpAddress> candidates = resolved->addresses;
  if (GetHappyEyeballsEnabledSetting() && candidates.size() > 1) {
    candidates = InterleaveForHappyEyeballs(candidates,
                                            ConnectStrategy::HappyEyeballs);
  }

  if (request.protocol == SocketProtocol::UDP) {
    auto sock = std::make_shared<SocketImpl>(SocketType::UDP);
    AddActiveSocket(sock);
    ConnectRequest creq;
    creq.address = candidates.front();
    creq.port = request.port;
    Status st = sock->Connect(creq);
    if (st != Status::OK) {
      (void)sock->Close();
      return st;
    }
    sockets_[sock->ServiceId()] = sock;
    ConnectToHostResponse resp;
    resp.socket = Socket::Client(*sock);
    resp.remote_address = candidates.front();
    return resp;
  }

  if (!GetHappyEyeballsEnabledSetting() || candidates.size() == 1) {
    Status last_err = Status::MISSING_MEDIA;
    for (const IpAddress& addr : candidates) {
      auto sock = std::make_shared<SocketImpl>(SocketType::TCP);
      AddActiveSocket(sock);
      ConnectRequest creq;
      creq.address = addr;
      creq.port = request.port;
      last_err = sock->Connect(creq);
      if (last_err == Status::OK) {
        sockets_[sock->ServiceId()] = sock;
        ConnectToHostResponse resp;
        resp.socket = Socket::Client(*sock);
        resp.remote_address = addr;
        return resp;
      }
      (void)sock->Close();
    }
    return last_err;
  }

  // RFC 8305 Happy Eyeballs v2 staggered connection racing (250 ms delay).
  struct RaceState {
    std::shared_ptr<SocketImpl> winner;
    IpAddress winner_addr;
    Status last_error = Status::MISSING_MEDIA;
    size_t completed = 0;
    size_t total = 0;
    bool done = false;
    std::vector<std::shared_ptr<SocketImpl>> in_flight;
    ::perception::Fiber* caller = nullptr;
  };
  auto state = std::make_shared<RaceState>();
  state->total = candidates.size();
  state->caller = ::perception::GetCurrentlyExecutingFiber();

  auto wake_caller = [state]() {
    if (state->caller != nullptr) {
      auto* caller = state->caller;
      state->caller = nullptr;
      caller->WakeUp();
    }
  };

  for (size_t i = 0; i < candidates.size(); ++i) {
    const IpAddress addr = candidates[i];
    const uint16 port = request.port;
    auto launch = [state, addr, port, wake_caller]() {
      if (state->done) {
        ++state->completed;
        return;
      }
      auto sock = std::make_shared<SocketImpl>(SocketType::TCP);
      AddActiveSocket(sock);
      state->in_flight.push_back(sock);
      ConnectRequest creq;
      creq.address = addr;
      creq.port = port;
      Status st = sock->Connect(creq);
      ++state->completed;
      if (st == Status::OK && !state->done) {
        state->done = true;
        state->winner = sock;
        state->winner_addr = addr;
        const auto to_close = state->in_flight;
        for (const auto& other : to_close) {
          if (other != sock) (void)other->Close();
        }
        wake_caller();
      } else {
        (void)sock->Close();
        state->last_error = st;
        if (state->completed == state->total && !state->done) {
          state->done = true;
          wake_caller();
        }
      }
    };
    if (i == 0) {
      ::perception::Defer(launch);
    } else {
      ::perception::AfterDuration(
          std::chrono::milliseconds(i * kHappyEyeballsAttemptDelayMs), launch);
    }
  }

  while (!state->done) ::perception::Sleep();
  state->caller = nullptr;

  if (state->winner) {
    sockets_[state->winner->ServiceId()] = state->winner;
    ConnectToHostResponse resp;
    resp.socket = Socket::Client(*state->winner);
    resp.remote_address = state->winner_addr;
    return resp;
  }
  return state->last_error;
}

StatusOr<GetInterfacesResponse> NetworkService::GetInterfaces() {
  GetInterfacesResponse response;
  const auto now = std::chrono::steady_clock::now();
  for (const NetworkInterface& nic : GetNetworkInterfaces()) {
    InterfaceInfo info;
    std::memcpy(info.mac.mac, nic.mac.data(), 6);
    info.mtu = nic.mtu;
    for (const InterfaceAddress& addr : nic.addresses) {
      InterfaceAddressInfo ainfo;
      ainfo.address = addr.address;
      ainfo.prefix_length = addr.prefix_length;
      ainfo.state = static_cast<uint8>(addr.state);
      ainfo.origin = static_cast<uint8>(addr.origin);
      info.addresses.push_back(ainfo);
    }
    for (const DefaultRouter& router : nic.default_routers) {
      if (now < router.valid_until)
        info.default_routers.push_back(router.address);
    }
    for (const DnsServer& dns : nic.dns_servers) {
      if (now < dns.valid_until) info.dns_servers.push_back(dns.address);
    }
    response.interfaces.push_back(std::move(info));
  }
  return response;
}
