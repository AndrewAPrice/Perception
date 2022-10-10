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

#include "linux_syscalls/pread64.h"

#include <iostream>
#include <sys/uio.h>

#include "linux_syscalls/readv.h"
#include "files.h"

namespace perception {
namespace linux_syscalls {

long pread64(int fd, void *buf, long count, off_t offset) {
  std::cout << "Reading " << count << " from " << offset <<std::endl;
  auto file = GetFileDescriptor(fd);
  if (!file || file->type != FileDescriptor::Type::FILE) {
    // File not open or not a file.
    return -1;
  }

  // Remember the old file offet, and seek to the new offset.
  size_t old_ofset = file->file.offset_in_file;
  file->file.offset_in_file = offset;


  // Read from the offset.
  iovec iov;
  iov.iov_base = buf;
  iov.iov_len = count;
  long bytes_read =  readv(fd, &iov, 1);

  // Return to the old offset.
  file->file.offset_in_file = old_ofset;
}

}  // namespace linux_syscalls
}  // namespace perception
