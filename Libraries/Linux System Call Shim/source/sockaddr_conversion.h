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

#include <netinet/in.h>
#include <sys/socket.h>
#include <types.h>

#include <optional>

#include "perception/network/ip_address.h"

namespace perception {

// Address and host-order port converted from or to a POSIX sockaddr.
struct SockaddrEndpoint {
  ::perception::network::IpAddress address;
  uint16 port = 0;
};

// Converts a `sockaddr_in` or `sockaddr_in6` into a `SockaddrEndpoint`,
// normalizing IPv4-mapped IPv6 addresses (`::ffff:a.b.c.d`) to `IpAddress::V4`.
// Returns nullopt if the family is unsupported or `addrlen` is too small.
std::optional<SockaddrEndpoint> SockaddrToEndpoint(const struct sockaddr* addr,
                                                   socklen_t addrlen);

// Writes `address` and `port` into `addr` and updates `*addrlen` per POSIX
// truncation semantics. Formats IPv4 addresses as IPv4-mapped `sockaddr_in6`
// when `socket_domain == AF_INET6`.
void EndpointToSockaddr(const ::perception::network::IpAddress& address,
                        uint16 port, int socket_domain, struct sockaddr* addr,
                        socklen_t* addrlen);

}  // namespace perception
