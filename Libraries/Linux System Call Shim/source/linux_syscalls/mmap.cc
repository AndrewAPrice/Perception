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

#include "linux_syscalls/mmap.h"

#include <memory>

#include "files.h"
#include "perception/debug.h"
#include "perception/memory.h"
#include "perception/services.h"
#include "perception/shared_memory.h"
#include "perception/storage_manager.h"
#include "sys/mman.h"

namespace perception {
namespace linux_syscalls {

long mmap(long addr, long length, long prot, long flags, long fd, long offset) {
  if (addr != 0) {
    perception::DebugPrinterSingleton
        << "mmap wants to place at a specific addr (" << (size_t)addr
        << ") but this isn't yet implemented.\n";
    errno = EINVAL;
    return -1;
  }

  if ((flags & MAP_PRIVATE) == 0) {
    perception::DebugPrinterSingleton
        << "mmap passed flags " << (size_t)flags
        << " but we don't support not setting MAP_PRIVATE.\n";
    errno = EINVAL;
    return -1;
  }

  if ((flags & ~(MAP_ANON | MAP_PRIVATE)) != 0) {
    perception::DebugPrinterSingleton << "mmap passed flags " << (size_t)flags
                                      << " but we don't support anything other "
                                         "than MAP_ANON and MAP_PRIVATE.\n";
    errno = EINVAL;
    return -1;
  }

  // 'prot' sepecifies if the memory can be executed, read, written, etc. The
  // kernel doesn't yet support this level of control, so we make all program
  // memory x/r/w and can ignore this parameter.

  if ((flags & MAP_ANON) != 0) {
    // Allocate 0-initialized memory.
    size_t pages = (size_t)(length + kPageSize - 1) / kPageSize;
    void *addr = AllocateMemoryPages(pages);
    memset(addr, 0, pages * kPageSize);
    return (long)addr;
  } else {
    // Allocate a memory mapped file.
    auto file_descriptor = GetFileDescriptor(fd);
    if (!file_descriptor) {
      // File not found.
      errno = EINVAL;
      return -1;
    }

    if (file_descriptor->type != FileDescriptor::FILE) {
      // Not a file.
      errno = EINVAL;
      return -1;
    }

    auto status_or_response = GetService<StorageManager>().OpenMemoryMappedFile(
        {file_descriptor->file.path});
    if (!status_or_response.Ok()) {
      errno = EINVAL;
      return -1;
    }

    return (long)AddMemoryMappedFile(status_or_response->file,
                                     status_or_response->file_contents);
  }
}

}  // namespace linux_syscalls
}  // namespace perception
