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

#include "linux_syscalls/accept4.h"

#include "linux_syscalls/accept.h"

namespace perception {
namespace linux_syscalls {

long accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags) {
  // accept4 just wraps around accept but could apply socket flags (like
  // SOCK_NONBLOCK/SOCK_CLOEXEC). For this stub, the accept shim is called
  // explicitly to resolve namespace ambiguity.
  return ::perception::linux_syscalls::accept(sockfd, addr, addrlen);
}

}  // namespace linux_syscalls
}  // namespace perception
