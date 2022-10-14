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
#include "perception/shared_memory.h"
#include "permebuf/Libraries/perception/storage_manager.permebuf.h"
#include "sys/mman.h"

using ::permebuf::perception::StorageManager;

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
    return (long)AllocateMemoryPages((size_t)(length + kPageSize - 1) /
                                     kPageSize);
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

    Permebuf<StorageManager::OpenMemoryMappedFileRequest> request;
    request->SetPath(file_descriptor->file.path);

    auto status_or_response =
        StorageManager::Get().CallOpenMemoryMappedFile(std::move(request));
    if (!status_or_response.Ok()) {
      errno = EINVAL;
      return -1;
    }

    return (long)AddMemoryMappedFile(
        status_or_response->GetFile(),
        std::make_unique<SharedMemory>(
            status_or_response->GetFileContents().Clone()));
  }
}

}  // namespace linux_syscalls
}  // namespace perception
