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

#include "perception/posix_network.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstring>
#include <string>

#include "files.h"
#include "perception/network/ip_address.h"
#include "perception/network/network_service.h"
#include "perception/services.h"

using ::perception::GetService;
using ::perception::network::ConnectToHostRequest;
using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;
using ::perception::network::NetworkService;
using ::perception::network::ResolveHostRequest;
using ::perception::network::SocketProtocol;

namespace perception {
namespace {

// Maximum number of addresses returned into musl's lookup_name buffer.
constexpr int kMaxMuslAddresses = 48;

// Layout matching `struct address` in musl's `source/network/lookup.h`.
struct MuslAddress {
  int family;
  unsigned scopeid;
  uint8 addr[16];
  int sortkey;
};

}  // namespace

int ConnectToHostAsFileDescriptor(std::string_view host, uint16 port) {
  auto network_service = GetService<NetworkService>();
  if (!network_service.IsValid()) {
    errno = ENETDOWN;
    return -1;
  }

  ConnectToHostRequest request;
  request.host = std::string(host);
  request.port = port;
  request.protocol = SocketProtocol::TCP;
  request.family = IpAddressFamily::Unspecified;

  auto response_or = network_service.ConnectToHost(request);
  if (!response_or) {
    errno = ECONNREFUSED;
    return -1;
  }

  int domain = response_or->remote_address.IsV6() ? AF_INET6 : AF_INET;
  return static_cast<int>(CreateSocketDescriptor(response_or->socket, domain));
}

extern "C" int __perception_lookup_name(void* buf, char canon[256],
                                        const char* name, int family,
                                        int flags) {
  (void)canon;
  (void)flags;
  if (buf == nullptr || name == nullptr || name[0] == '\0')
    return EAI_NONAME;

  IpAddressFamily req_family = IpAddressFamily::Unspecified;
  if (family == AF_INET) {
    req_family = IpAddressFamily::V4;
  } else if (family == AF_INET6) {
    req_family = IpAddressFamily::V6;
  } else if (family != AF_UNSPEC) {
    return EAI_FAMILY;
  }

  auto network_service = GetService<NetworkService>();
  if (!network_service.IsValid())
    return EAI_AGAIN;

  ResolveHostRequest request;
  request.host = name;
  request.family = req_family;

  auto response_or = network_service.ResolveHost(request);
  if (!response_or || response_or->addresses.empty())
    return EAI_NONAME;

  auto* out = static_cast<MuslAddress*>(buf);
  int count = 0;
  for (const IpAddress& addr : response_or->addresses) {
    if (count >= kMaxMuslAddresses)
      break;
    if (addr.IsV4()) {
      if (family == AF_INET6)
        continue;
      out[count].family = AF_INET;
      out[count].scopeid = 0;
      std::memset(out[count].addr, 0, sizeof(out[count].addr));
      std::memcpy(out[count].addr, addr.bytes().data(), IpAddress::kV4Length);
      out[count].sortkey = 0;
      count++;
    } else if (addr.IsV6()) {
      if (family == AF_INET)
        continue;
      out[count].family = AF_INET6;
      out[count].scopeid = 0;
      std::memcpy(out[count].addr, addr.bytes().data(), IpAddress::kV6Length);
      out[count].sortkey = 0;
      count++;
    }
  }

  return count > 0 ? count : EAI_NONAME;
}

}  // namespace perception
