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

#include "perception/linux_syscalls/writev.h"

#include "perception/debug.h"

namespace perception {
namespace linux_syscalls {

long writev(long file_descriptor, struct iovec* buffers, long buffer_count) {
	size_t bytes_written = 0;
	for (size_t i = 0; i < buffer_count; i++) {
		const auto& buffer = buffers[i];

		char* c = (char *)buffer.iov_base;
		for (size_t j = 0; j < buffer.iov_len; j++, c++, bytes_written++) {
			DebugPrinterSingleton << *c;
		}
	}
	return bytes_written;
}

}
}
