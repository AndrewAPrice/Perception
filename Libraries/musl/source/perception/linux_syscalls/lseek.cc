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

#include "perception/linux_syscalls/lseek.h"

#include <iostream>

#include "perception/files.h"

namespace perception {
namespace linux_syscalls {

off_t lseek(long fd, off_t offset, int whence) {
	auto file = GetFileDescriptor(fd);
	if (!file || file->type != FileDescriptor::Type::FILE) {
		// File not open or not a file.
		return -1;
	}

	switch (whence) {
		case SEEK_SET:
			file->file.offset_in_file = offset;
			break;
		case SEEK_CUR:
			file->file.offset_in_file += offset;
			break;
		case SEEK_END:
			file->file.offset_in_file = file->file.size_in_bytes + offset;
			break;
		default:
			std::cout << "Unknown whence passed to lseek: " << whence << std::endl;
			return -1;
	}

	return (off_t)file->file.offset_in_file;
}

}
}
