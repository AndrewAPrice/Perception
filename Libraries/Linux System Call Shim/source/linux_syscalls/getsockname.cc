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

#include "linux_syscalls/getsockname.h"

#include <errno.h>

#include "files.h"
#include "sockaddr_conversion.h"

namespace perception {
namespace linux_syscalls {

long getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  if (addr == nullptr || addrlen == nullptr)
    return -EFAULT;
  auto descriptor = GetFileDescriptor(sockfd);
  if (!descriptor)
    return -EBADF;
  if (descriptor->type != FileDescriptor::SOCKET)
    return -ENOTSOCK;

  auto endpoints_or = descriptor->socket.socket.GetEndpoints();
  if (!endpoints_or)
    return -EINVAL;

  const auto& endpoints = *endpoints_or;
  EndpointToSockaddr(endpoints.local_address, endpoints.local_port,
                     descriptor->socket.domain, addr, addrlen);
  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
