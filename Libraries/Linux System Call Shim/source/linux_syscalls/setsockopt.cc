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

#include "linux_syscalls/setsockopt.h"

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "files.h"

namespace perception {
namespace linux_syscalls {

long setsockopt(int sockfd, int level, int optname, const void* optval,
                socklen_t optlen) {
  auto descriptor = GetFileDescriptor(sockfd);
  if (!descriptor)
    return -EBADF;
  if (descriptor->type != FileDescriptor::SOCKET)
    return -ENOTSOCK;

  if (level == IPPROTO_IPV6 && optname == IPV6_V6ONLY) {
    if (optval == nullptr)
      return -EFAULT;
    if (optlen < static_cast<socklen_t>(sizeof(int)))
      return -EINVAL;
    descriptor->socket.ipv6_v6only = (*static_cast<const int*>(optval) != 0);
    return 0;
  }

  if (level == SOL_SOCKET &&
      (optname == SO_REUSEADDR || optname == SO_KEEPALIVE)) {
    return 0;
  }

  if (level == IPPROTO_TCP && optname == TCP_NODELAY)
    return 0;

  return -ENOPROTOOPT;
}

}  // namespace linux_syscalls
}  // namespace perception
