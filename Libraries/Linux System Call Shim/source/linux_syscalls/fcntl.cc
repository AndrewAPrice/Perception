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

#include <fcntl.h>
#include <unistd.h>

#include "perception/debug.h"

#include "files.h"

namespace perception {
namespace linux_syscalls {

long fcntl(long fd, long cmd, long arg) {
  auto file_descriptor = GetFileDescriptor(fd);
  if (!file_descriptor) return 0;

  switch (cmd) {
    case F_DUPFD:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_DUPFD\n";
      break;
    case F_DUPFD_CLOEXEC:
      perception::DebugPrinterSingleton << "Musl syscall fnctl called with unimplemented command "
                   "F_DUPFD_CLOEXEC\n";
      break;
    case F_GETFD:
    case F_SETFD:
      // The only flag supported is FD_CLOEXEC.
      if (arg != FD_CLOEXEC) {
        perception::DebugPrinterSingleton << "Musl syscall fnctl/F_{GET|SET}FD called with arg other "
                     "than FD_CLOEXEC.\n";
      }
      break;
    case F_GETFL:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_GETFL\n";
      break;
    case F_SETFL:
      perception::DebugPrinterSingleton
          << "Musl syscall fnctl called with unimplemented command F_SETFL\n";
      break;
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

