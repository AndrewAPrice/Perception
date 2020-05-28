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

#include "perception/linux_syscalls/mmap.h"

#include "perception/debug.h"
#include "perception/memory.h"
#include "sys/mman.h"

namespace perception {
namespace linux_syscalls {

long mmap(long addr, long length, long prot, long flags,
                  long fd, long offset) {

	if (addr != 0) {
		perception::DebugPrinterSingleton << "mmap wants to place at a specific addr (" << (size_t)addr << ") but this isn't yet implemented.\n";
		return 0;
	}

	if (flags != (MAP_ANON | MAP_PRIVATE)) {
		perception::DebugPrinterSingleton << "mmap passed flags " << (size_t)flags << " but currently only MAP_ANON | MAP_FIXED is supported.\n";
	}

	// 'prot' sepecifies if the memory can be executed, read, written, etc. The kernel doesn't yet support this level
	// of control, so we make all program memory x/r/w and can ignore this parameter.

	return (long)AllocateMemoryPages((size_t)length / kPageSize);
}

}
}

