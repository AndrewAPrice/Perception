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
  // Map type to standard protocol enum.
  SocketProtocol socket_protocol = SocketProtocol::TCP;
  if (type == SOCK_DGRAM) {
    socket_protocol = SocketProtocol::UDP;
  }

  // Contact the NetworkService RPC explicitly
  CreateSocketRequest request;
  request.protocol = socket_protocol;

  auto status_or_res = GetService<NetworkService>().CreateSocket(request);
  if (!status_or_res) {
    errno = ENETDOWN;
    return -1;
  }

  // Create a file descriptor mapping to the Socket Client reference
  long fd = CreateSocketDescriptor(status_or_res->socket);
  return fd;
}

}  // namespace linux_syscalls
}  // namespace perception
