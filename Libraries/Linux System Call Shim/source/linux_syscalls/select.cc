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

#include "linux_syscalls/select.h"

#include <sys/time.h>

#include "perception/time.h"

namespace perception {
namespace linux_syscalls {

long select(int nfds, void* readfds, void* writefds, void* exceptfds,
            struct ::timeval* timeout) {
  if (timeout) {
    long long microseconds =
        static_cast<long long>(timeout->tv_sec) * 1000000LL +
        static_cast<long long>(timeout->tv_usec);
    if (microseconds > 0) {
      SleepForDuration(std::chrono::microseconds(microseconds));
    }
  }
  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
