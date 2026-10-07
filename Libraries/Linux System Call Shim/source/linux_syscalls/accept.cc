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

#include "linux_syscalls/accept.h"

#include <errno.h>
#include <netinet/in.h>

#include "files.h"
#include "sockaddr_conversion.h"

namespace perception {
namespace linux_syscalls {

using ::perception::network::Socket;

long accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  auto descriptor = GetFileDescriptor(sockfd);
  if (!descriptor || descriptor->type != FileDescriptor::SOCKET) return -EBADF;

  auto status_or_response = descriptor->socket.socket.Accept();
  if (!status_or_response) return -ECONNABORTED;

  if (addr && addrlen) {
    EndpointToSockaddr(status_or_response->remote_address,
                       status_or_response->remote_port,
                       descriptor->socket.domain, addr, addrlen);
  }

  Socket::Client client(status_or_response->process_id,
                        status_or_response->message_id);
  return CreateSocketDescriptor(client, descriptor->socket.domain);
}

}  // namespace linux_syscalls
}  // namespace perception
