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

#include "linux_syscalls/fstatfs.h"

#include <sys/vfs.h>

#include <iostream>

#include "files.h"

namespace perception {
namespace linux_syscalls {

long fstatfs(int fd, struct statfs* buf) {
  auto descriptor = GetFileDescriptor(fd);
  if (!descriptor || descriptor->type != FileDescriptor::FILE) {
    errno = EINVAL;
    return -1;
  }

  if (descriptor->type != FileDescriptor::DIRECTORY) {
    errno = ENOTDIR;
    return -1;
  }
  std::cout << "System call fstatfs is unimplemented. Called for "
            << descriptor->directory.name << std::endl;

  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
