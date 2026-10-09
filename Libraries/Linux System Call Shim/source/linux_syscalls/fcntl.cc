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

#include "linux_syscalls/fcntl.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "files.h"
#include "perception/debug.h"

namespace perception {
namespace linux_syscalls {

long fcntl(long fd, long cmd, long arg) {
  auto file_descriptor = GetFileDescriptor(fd);
  if (!file_descriptor) {
    if (fd >= 0 && fd <= 2) {
      if (cmd == F_GETFL)
        return fd == 0 ? O_RDONLY : O_WRONLY;
      if (cmd == F_GETFD || cmd == F_SETFD || cmd == F_SETFL)
        return 0;
    }
    errno = EBADF;
    return -EBADF;
  }

  switch (cmd) {
    case F_DUPFD:
      if (arg < 0) {
        errno = EINVAL;
        return -EINVAL;
      }
      return DuplicateFileDescriptor(static_cast<int>(fd),
                                     static_cast<int>(arg), false, 0);
    case F_DUPFD_CLOEXEC:
      if (arg < 0) {
        errno = EINVAL;
        return -EINVAL;
      }
      return DuplicateFileDescriptor(static_cast<int>(fd),
                                     static_cast<int>(arg), false, O_CLOEXEC);
    case F_GETFD:
    case F_SETFD:
      return 0;
    case F_GETFL:
      if (file_descriptor->type == FileDescriptor::SOCKET)
        return file_descriptor->socket.non_blocking ? O_NONBLOCK : 0;
      if (file_descriptor->type == FileDescriptor::PIPE) {
        long flags = file_descriptor->pipe.is_writer ? O_WRONLY : O_RDONLY;
        if (file_descriptor->pipe.non_blocking)
          flags |= O_NONBLOCK;
        return flags;
      }
      return 0;
    case F_SETFL:
      if (file_descriptor->type == FileDescriptor::SOCKET)
        file_descriptor->socket.non_blocking = (arg & O_NONBLOCK) != 0;
      else if (file_descriptor->type == FileDescriptor::PIPE)
        file_descriptor->pipe.non_blocking = (arg & O_NONBLOCK) != 0;
      return 0;
    case F_SETLK:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_SETLK\n";
      break;
    case F_SETLKW:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_SETLKW\n";
      break;
    case F_GETLK:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_GETLK\n";
      break;
    case F_OFD_SETLK:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_OFD_SETLK\n";
      break;
    case F_OFD_SETLKW:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_OFD_SETLKW\n";
      break;
    case F_OFD_GETLK:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_OFD_GETLK\n";
      break;
    case F_GETOWN:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_GETOWN\n";
      break;
    case F_SETOWN:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_SETOWN\n";
      break;
    case F_GETOWN_EX:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_GETOWN_EX\n";
      break;
    case F_SETOWN_EX:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_SETOWN_EX\n";
      break;
    case F_GETSIG:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_GETSIG\n";
      break;
    case F_SETSIG:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_SETSIG\n";
      break;
    case F_SETLEASE:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_SETLEASE\n";
      break;
    case F_GETLEASE:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_GETLEASE\n";
      break;
    case F_NOTIFY:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_NOTIFY\n";
      break;
    case F_SETPIPE_SZ:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_SETPIPE_SZ\n";
      break;
    case F_GETPIPE_SZ:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_GETPIPE_SZ\n";
      break;
    case F_ADD_SEALS:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_ADD_SEALS\n";
      break;
    case F_GET_SEALS:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_GET_SEALS\n";
      break;
    case F_GET_RW_HINT:
      perception::DebugPrinterSingleton << "Musl syscall fnctl called with unimplemented command "
                   "F_GET_RW_HINT\n";
      break;
    case F_SET_RW_HINT:
      perception::DebugPrinterSingleton << "Musl syscall fnctl called with unimplemented command "
                   "F_SET_RW_HINT\n";
      break;
    case F_GET_FILE_RW_HINT:
      perception::DebugPrinterSingleton << "Musl syscall fnctl called with unimplemented command "
                   "F_GET_FILE_RW_HINT\n";
      break;
    case F_SET_FILE_RW_HINT:
      perception::DebugPrinterSingleton << "Musl syscall fnctl called with unimplemented command "
                   "F_SET_FILE_RW_HINT\n";
      break;
    default:
      perception::DebugPrinterSingleton << "Musl syscall fnctl called with unknown command " << (size_t)cmd
                << '\n';
  }

  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception

