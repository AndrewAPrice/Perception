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

#include "sockaddr_conversion.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace perception {
namespace {

// Returns true if `bytes` is an IPv4-mapped IPv6 address (::ffff:a.b.c.d).
bool IsV4MappedBytes(const uint8* bytes) {
  for (size_t i = 0; i < 10; i++) {
    if (bytes[i] != 0) return false;
  }
  return bytes[10] == 0xFF && bytes[11] == 0xFF;
}

}  // namespace

std::optional<SockaddrEndpoint> SockaddrToEndpoint(const struct sockaddr* addr,
                                                   socklen_t addrlen) {
  if (addr == nullptr || addrlen < sizeof(sa_family_t)) return std::nullopt;

  if (addr->sa_family == AF_INET) {
    if (addrlen < sizeof(struct sockaddr_in)) return std::nullopt;
    const auto* sin = reinterpret_cast<const struct sockaddr_in*>(addr);
    const auto* raw = reinterpret_cast<const uint8*>(&sin->sin_addr);
    SockaddrEndpoint endpoint;
    endpoint.address = IpAddress::FromBytes(
        IpAddressFamily::V4, std::span<const uint8>(raw, IpAddress::kV4Length));
    endpoint.port = ntohs(sin->sin_port);
    return endpoint;
  }

  if (addr->sa_family == AF_INET6) {
    if (addrlen < sizeof(struct sockaddr_in6)) return std::nullopt;
    const auto* sin6 = reinterpret_cast<const struct sockaddr_in6*>(addr);
    const uint8* raw = sin6->sin6_addr.s6_addr;
    SockaddrEndpoint endpoint;
    if (IsV4MappedBytes(raw)) {
      endpoint.address = IpAddress::V4(raw[12], raw[13], raw[14], raw[15]);
    } else {
      std::array<uint8, IpAddress::kV6Length> v6_bytes{};
      std::memcpy(v6_bytes.data(), raw, IpAddress::kV6Length);
      endpoint.address = IpAddress::V6(v6_bytes);
    }
    endpoint.port = ntohs(sin6->sin6_port);
    return endpoint;
  }

  return std::nullopt;
}

void EndpointToSockaddr(const IpAddress& address, uint16 port,
                        int socket_domain, struct sockaddr* addr,
                        socklen_t* addrlen) {
  if (addr == nullptr || addrlen == nullptr) return;

  if (socket_domain == AF_INET6 || address.IsV6()) {
    struct sockaddr_in6 sin6{};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(port);
    if (address.IsV6()) {
      std::memcpy(sin6.sin6_addr.s6_addr, address.bytes().data(),
                  IpAddress::kV6Length);
    } else if (address.IsV4() && !address.IsUnspecified()) {
      sin6.sin6_addr.s6_addr[10] = 0xFF;
      sin6.sin6_addr.s6_addr[11] = 0xFF;
      std::memcpy(&sin6.sin6_addr.s6_addr[12], address.bytes().data(),
                  IpAddress::kV4Length);
    }
    size_t copy_len = std::min<size_t>(*addrlen, sizeof(sin6));
    std::memcpy(addr, &sin6, copy_len);
    *addrlen = sizeof(sin6);
    return;
  }

  struct sockaddr_in sin{};
  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  if (address.IsV4()) {
    std::memcpy(&sin.sin_addr, address.bytes().data(), IpAddress::kV4Length);
  }
  size_t copy_len = std::min<size_t>(*addrlen, sizeof(sin));
  std::memcpy(addr, &sin, copy_len);
  *addrlen = sizeof(sin);
}

}  // namespace perception
