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
#include "perception/debug.h"

namespace perception {
namespace linux_syscalls {

using ::perception::network::ConnectRequest;

long connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  auto descriptor = GetFileDescriptor(sockfd);
  if (!descriptor || descriptor->type != FileDescriptor::SOCKET) {
    errno = EBADF;
    return -1;
  }

  if (addr->sa_family != AF_INET) {
    errno = EAFNOSUPPORT;
    return -1;
  }

  const struct sockaddr_in* addr_in = (const struct sockaddr_in*)addr;
  uint32 s_addr = addr_in->sin_addr.s_addr;

  ConnectRequest request;
  request.address.address[0] = (s_addr & 0x000000FF);
  request.address.address[1] = (s_addr & 0x0000FF00) >> 8;
  request.address.address[2] = (s_addr & 0x00FF0000) >> 16;
  request.address.address[3] = (s_addr & 0xFF000000) >> 24;
  request.port = ntohs(addr_in->sin_port);

  auto status = descriptor->socket.socket.Connect(request);
  if (status != Status::OK) {
    errno = ECONNREFUSED;
    return -1;
  }

  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
