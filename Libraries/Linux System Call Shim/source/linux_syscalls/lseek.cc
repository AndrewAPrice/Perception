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

#include "linux_syscalls/lseek.h"

#include <errno.h>

#include "files.h"
#include "perception/debug.h"

namespace perception {
namespace linux_syscalls {

off_t lseek(long fd, off_t offset, int whence) {
  auto file = GetFileDescriptor(fd);
  if (!file)
    return -EBADF;
  if (file->type == FileDescriptor::Type::PIPE ||
      file->type == FileDescriptor::Type::SOCKET)
    return -ESPIPE;
  if (file->type != FileDescriptor::Type::FILE)
    return -EINVAL;

  off_t new_offset = 0;
  switch (whence) {
    case SEEK_SET:
      new_offset = offset;
      break;
    case SEEK_CUR:
      new_offset = static_cast<off_t>(file->file.offset_in_file) + offset;
      break;
    case SEEK_END:
      new_offset = static_cast<off_t>(file->file.size_in_bytes) + offset;
      break;
    default:
      perception::DebugPrinterSingleton << "Unknown whence passed to lseek: "
                                        << (size_t)whence << '\n';
      return -EINVAL;
  }

  if (new_offset < 0)
    return -EINVAL;

  file->file.offset_in_file = static_cast<size_t>(new_offset);
  return new_offset;
}

}  // namespace linux_syscalls
}  // namespace perception
