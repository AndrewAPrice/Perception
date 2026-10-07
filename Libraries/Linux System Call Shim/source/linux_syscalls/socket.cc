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

#include "linux_syscalls/socket.h"

#include <errno.h>
#include <sys/socket.h>

#include "files.h"
#include "perception/network/network_service.h"
#include "perception/services.h"

using ::perception::GetService;
using ::perception::network::CreateSocketRequest;
using ::perception::network::NetworkService;
using ::perception::network::SocketProtocol;

namespace perception {
namespace linux_syscalls {

long socket(int domain, int type, int protocol) {
  (void)protocol;
  if (domain != AF_INET && domain != AF_INET6) return -EAFNOSUPPORT;

  int base_type = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  SocketProtocol socket_protocol = SocketProtocol::TCP;
  if (base_type == SOCK_DGRAM) {
    socket_protocol = SocketProtocol::UDP;
  } else if (base_type != SOCK_STREAM) {
    return -EPROTONOSUPPORT;
  }

  CreateSocketRequest request;
  request.protocol = socket_protocol;

  auto status_or_res = GetService<NetworkService>().CreateSocket(request);
  if (!status_or_res) return -ENETDOWN;

  long fd = CreateSocketDescriptor(status_or_res->socket, domain);
  if ((type & SOCK_NONBLOCK) != 0) {
    if (auto descriptor = GetFileDescriptor(fd))
      descriptor->socket.non_blocking = true;
  }
  return fd;
}

}  // namespace linux_syscalls
}  // namespace perception
