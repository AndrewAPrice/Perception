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

#include "perception/linux_syscalls/open.h"

#include <iostream>

#include "perception/files.h"

namespace perception {
namespace linux_syscalls {

long open(const char* pathname, int flags, mode_t mode) {
	// Flags that are safe to ignore: D_CLOEXEC, D_TMPFILE
	if (flags & O_DIRECTORY) {
		return OpenDirectory(pathname);
	} else if (flags == 0) {
		long id = OpenFile(pathname);
		if (id == 0) {
			std::cout << "Can't open file" << std::endl;
			return EACCES;
		}
			std::cout << "Open file" << std::endl;
		return id;
	} else {
		std::cout << "Invoking MUSL syncall open() on " << pathname << " with flags:";
		if (flags & O_APPEND) std::cout << " O_APPEND";
		if (flags & O_ASYNC) std::cout << " O_ASYNC";
		if (flags & O_CREAT) std::cout << " O_CREAT";
		if (flags & O_CLOEXEC) std::cout << " O_CLOEXEC";
		if (flags & O_DIRECT) std::cout << " O_DIRECT";
		if (flags & O_DIRECTORY) std::cout << " O_DIRECTORY";
		if (flags & O_DSYNC) std::cout << " O_DSYNC";
		if (flags & O_EXCL) std::cout << " O_EXCL";
		if (flags & O_LARGEFILE) std::cout << " O_LARGEFILE";
		if (flags & O_NOATIME) std::cout << " O_NOATIME";
		if (flags & O_NOCTTY) std::cout << " O_NOCTTY";
		if (flags & O_NOFOLLOW) std::cout << " O_NOFOLLOW";
		if (flags & O_NONBLOCK) std::cout << " O_NONBLOCK";
		if (flags & O_NDELAY) std::cout << " O_NDELAY";
		if (flags & O_PATH) std::cout << " O_PATH";
		if (flags & O_SYNC) std::cout << " O_SYNC";
		if (flags & O_TMPFILE) std::cout << " O_TMPFILE";
		if (flags & O_TRUNC) std::cout << " O_TRUNC";
		std::cout << std::endl;
		return 0;
	}
}

}
}
