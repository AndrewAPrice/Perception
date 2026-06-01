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

#include <errno.h>
#include "perception/debug.h"

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
    if (id < 0) {
      return id;
    }
    return id;
  } else {
    perception::DebugPrinterSingleton << "Invoking MUSL syscall open() on " << pathname
              << " with flags:";
    if (flags & O_APPEND) perception::DebugPrinterSingleton << " O_APPEND";
    if (flags & O_ASYNC) perception::DebugPrinterSingleton << " O_ASYNC";
    if (flags & O_CREAT) perception::DebugPrinterSingleton << " O_CREAT";
    if (flags & O_DIRECT) perception::DebugPrinterSingleton << " O_DIRECT";
    if (flags & O_DIRECTORY) perception::DebugPrinterSingleton << " O_DIRECTORY";
    if (flags & O_DSYNC) perception::DebugPrinterSingleton << " O_DSYNC";
    if (flags & O_EXCL) perception::DebugPrinterSingleton << " O_EXCL";
    if (flags & O_NOATIME) perception::DebugPrinterSingleton << " O_NOATIME";
    if (flags & O_NOCTTY) perception::DebugPrinterSingleton << " O_NOCTTY";
    if (flags & O_NOFOLLOW) perception::DebugPrinterSingleton << " O_NOFOLLOW";
    if (flags & O_NONBLOCK) perception::DebugPrinterSingleton << " O_NONBLOCK";
    if (flags & O_NDELAY) perception::DebugPrinterSingleton << " O_NDELAY";
    if (flags & O_PATH) perception::DebugPrinterSingleton << " O_PATH";
    if (flags & O_SYNC) perception::DebugPrinterSingleton << " O_SYNC";
    if (flags & O_TRUNC) perception::DebugPrinterSingleton << " O_TRUNC";
    perception::DebugPrinterSingleton << '\n';
    return -EINVAL;
  }
}

}  // namespace linux_syscalls
}  // namespace perception
