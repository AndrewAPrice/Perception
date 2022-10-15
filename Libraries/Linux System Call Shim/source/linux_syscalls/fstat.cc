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

#include "linux_syscalls/fstat.h"

#include <sys/stat.h>

#include "files.h"
#include "perception/debug.h"

namespace perception {
namespace linux_syscalls {

long fstat(long fd, struct stat* statbuf) {
  auto file_descriptor = GetFileDescriptor(fd);
  if (!file_descriptor) {
    errno = EINVAL;
    return -1;
  }

  memset(statbuf, 0, sizeof(struct stat));
  if (file_descriptor->type == FileDescriptor::DIRECTORY) {
    statbuf->st_mode = S_IFDIR;
  } else if (file_descriptor->type == FileDescriptor::FILE) {
    statbuf->st_mode = S_IFREG;
    statbuf->st_size = file_descriptor->file.size_in_bytes;
  }

  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
