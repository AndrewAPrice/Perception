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
#include <fcntl.h>

#include "files.h"
#include "perception/debug.h"

namespace perception {
namespace linux_syscalls {

long open(const char* pathname, int flags, mode_t mode) {
  if (flags & O_DIRECTORY) {
    return OpenDirectory(pathname);
  }

  // Parse open flags:
  bool read_access = false;
  bool write_access = false;
  bool create_if_not_exists = (flags & O_CREAT) != 0;
  bool truncate = (flags & O_TRUNC) != 0;

  int access_mode = flags & O_ACCMODE;
  switch (access_mode) {
    case O_RDONLY:
      read_access = true;
      break;
    case O_WRONLY:
      write_access = true;
      break;
    case O_RDWR:
      read_access = true;
      write_access = true;
      break;
    default:
      perception::DebugPrinterSingleton
          << "Invoking MUSL syscall open() on " << pathname
          << " with unsupported access mode: "
          << static_cast<int64>(access_mode) << '\n';
      return -EINVAL;
  }

  // Flags that are safe to ignore:
  int ignored_flags =
      O_CLOEXEC | O_TMPFILE | O_LARGEFILE | O_CREAT | O_TRUNC | O_ACCMODE;
  // If write or read-write access is used, we also ignore append or nonblock
  // for now
  ignored_flags |= O_APPEND | O_NONBLOCK | O_NDELAY;

  int unsupported_flags = flags & ~ignored_flags;

  if (unsupported_flags != 0) {
    perception::DebugPrinterSingleton << "Invoking MUSL syscall open() on "
                                      << pathname << " with unsupported flags:";
    if (unsupported_flags & O_ASYNC)
      perception::DebugPrinterSingleton << " O_ASYNC";
    if (unsupported_flags & O_DIRECT)
      perception::DebugPrinterSingleton << " O_DIRECT";
    if (unsupported_flags & O_DSYNC)
      perception::DebugPrinterSingleton << " O_DSYNC";
    if (unsupported_flags & O_EXCL)
      perception::DebugPrinterSingleton << " O_EXCL";
    if (unsupported_flags & O_NOATIME)
      perception::DebugPrinterSingleton << " O_NOATIME";
    if (unsupported_flags & O_NOCTTY)
      perception::DebugPrinterSingleton << " O_NOCTTY";
    if (unsupported_flags & O_NOFOLLOW)
      perception::DebugPrinterSingleton << " O_NOFOLLOW";
    if (unsupported_flags & O_PATH)
      perception::DebugPrinterSingleton << " O_PATH";
    if (unsupported_flags & O_SYNC)
      perception::DebugPrinterSingleton << " O_SYNC";
    perception::DebugPrinterSingleton << '\n';
    return -EINVAL;
  }

  return OpenFile(pathname, read_access, write_access, create_if_not_exists,
                  truncate);
}

}  // namespace linux_syscalls
}  // namespace perception
