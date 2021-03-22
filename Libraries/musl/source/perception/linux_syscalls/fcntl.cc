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

#include "perception/linux_syscalls/fcntl.h"

#include <iostream>
#include <fcntl.h>
#include <unistd.h>

#include "perception/files.h"

namespace perception {
namespace linux_syscalls {

long fcntl(long fd, long cmd, long arg) {
	auto file_descriptor = GetFileDescriptor(fd);
	if (!file_descriptor)
		return 0;

	switch (cmd) {
		case F_DUPFD:
			std::cout << "Musl syscall fnctl called with unimplemented command F_DUPFD" << std::endl;
			break;
		case F_DUPFD_CLOEXEC:
			std::cout << "Musl syscall fnctl called with unimplemented command F_DUPFD_CLOEXEC" << std::endl;
			break;
		case F_GETFD:
		case F_SETFD:
			// The only flag supported is FD_CLOEXEC.
			if (arg != FD_CLOEXEC) {
				std::cout << "Musl syscall fnctl/F_{GET|SET}FD called with arg other than FD_CLOEXEC." << std::endl;
			}
			break;
		case F_GETFL:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GETFL" << std::endl;
			break;
		case F_SETFL:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SETFL" << std::endl;
			break;
		case F_SETLK:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SETLK" << std::endl;
			break;
		case F_SETLKW:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SETLKW" << std::endl;
			break;
		case F_GETLK:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GETLK" << std::endl;
			break;
		case F_OFD_SETLK:
			std::cout << "Musl syscall fnctl called with unimplemented command F_OFD_SETLK" << std::endl;
			break;
		case F_OFD_SETLKW:
			std::cout << "Musl syscall fnctl called with unimplemented command F_OFD_SETLKW" << std::endl;
			break;
		case F_OFD_GETLK:
			std::cout << "Musl syscall fnctl called with unimplemented command F_OFD_GETLK" << std::endl;
			break;
		case F_GETOWN:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GETOWN" << std::endl;
			break;
		case F_SETOWN:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SETOWN" << std::endl;
			break;
		case F_GETOWN_EX:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GETOWN_EX" << std::endl;
			break;
		case F_SETOWN_EX:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SETOWN_EX" << std::endl;
			break;
		case F_GETSIG:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GETSIG" << std::endl;
			break;
		case F_SETSIG:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SETSIG" << std::endl;
			break;
		case F_SETLEASE:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SETLEASE" << std::endl;
			break;
		case F_GETLEASE:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GETLEASE" << std::endl;
			break;
		case F_NOTIFY:
			std::cout << "Musl syscall fnctl called with unimplemented command F_NOTIFY" << std::endl;
			break;
		case F_SETPIPE_SZ:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SETPIPE_SZ" << std::endl;
			break;
		case F_GETPIPE_SZ:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GETPIPE_SZ" << std::endl;
			break;
		case F_ADD_SEALS:
			std::cout << "Musl syscall fnctl called with unimplemented command F_ADD_SEALS" << std::endl;
			break;
		case F_GET_SEALS:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GET_SEALS" << std::endl;
			break;
		case F_GET_RW_HINT:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GET_RW_HINT" << std::endl;
			break;
		case F_SET_RW_HINT:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SET_RW_HINT" << std::endl;
			break;
		case F_GET_FILE_RW_HINT:
			std::cout << "Musl syscall fnctl called with unimplemented command F_GET_FILE_RW_HINT" << std::endl;
			break;
		case F_SET_FILE_RW_HINT:
			std::cout << "Musl syscall fnctl called with unimplemented command F_SET_FILE_RW_HINT" << std::endl;
			break;
		default:
			std::cout << "Musl syscall fnctl called with unknown command " << cmd << std::endl;
	}

	return 0;
}

}
}
