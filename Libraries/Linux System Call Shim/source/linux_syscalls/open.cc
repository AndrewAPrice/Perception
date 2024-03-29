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

#include "linux_syscalls/open.h"

#include <iostream>

#include "files.h"

namespace perception {
namespace linux_syscalls {

long open(const char* pathname, int flags, mode_t mode) {
  if (flags & O_DIRECTORY) {
    return OpenDirectory(pathname);
  }

  // Flags that are safe to ignore:
  flags &= ~(O_CLOEXEC | O_TMPFILE | O_LARGEFILE);

  if (flags == 0) {
    long id = OpenFile(pathname);
    if (id == -1) errno = EINVAL;
    return id;
  } else {
    std::cout << "Invoking MUSL syscall open() on " << pathname
              << " with flags:";
    if (flags & O_APPEND) std::cout << " O_APPEND";
    if (flags & O_ASYNC) std::cout << " O_ASYNC";
    if (flags & O_CREAT) std::cout << " O_CREAT";
    if (flags & O_DIRECT) std::cout << " O_DIRECT";
    if (flags & O_DIRECTORY) std::cout << " O_DIRECTORY";
    if (flags & O_DSYNC) std::cout << " O_DSYNC";
    if (flags & O_EXCL) std::cout << " O_EXCL";
    if (flags & O_NOATIME) std::cout << " O_NOATIME";
    if (flags & O_NOCTTY) std::cout << " O_NOCTTY";
    if (flags & O_NOFOLLOW) std::cout << " O_NOFOLLOW";
    if (flags & O_NONBLOCK) std::cout << " O_NONBLOCK";
    if (flags & O_NDELAY) std::cout << " O_NDELAY";
    if (flags & O_PATH) std::cout << " O_PATH";
    if (flags & O_SYNC) std::cout << " O_SYNC";
    if (flags & O_TRUNC) std::cout << " O_TRUNC";
    std::cout << std::endl;
    errno = EINVAL;
    return -1;
  }
}

}  // namespace linux_syscalls
}  // namespace perception
