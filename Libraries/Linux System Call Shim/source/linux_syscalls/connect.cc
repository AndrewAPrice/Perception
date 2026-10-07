// Copyright 2020 Google LLC
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

#include "linux_syscalls/connect.h"

#include <errno.h>
#include <netinet/in.h>

#include "files.h"
#include "sockaddr_conversion.h"

namespace perception {
namespace linux_syscalls {

using ::perception::network::ConnectRequest;

long connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  auto descriptor = GetFileDescriptor(sockfd);
  if (!descriptor || descriptor->type != FileDescriptor::SOCKET) return -EBADF;

  auto endpoint = SockaddrToEndpoint(addr, addrlen);
  if (!endpoint.has_value()) return -EAFNOSUPPORT;

  if (descriptor->socket.domain == AF_INET && endpoint->address.IsV6())
    return -EAFNOSUPPORT;
  if (descriptor->socket.domain == AF_INET6 && descriptor->socket.ipv6_v6only &&
      endpoint->address.IsV4())
    return -EAFNOSUPPORT;

  ConnectRequest request;
  request.address = endpoint->address;
  request.port = endpoint->port;

  auto status = descriptor->socket.socket.Connect(request);
  if (status != Status::OK) return -ECONNREFUSED;

  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
