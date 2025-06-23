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

#include "linux_syscalls/getdents64.h"

#include <iostream>

#include "files.h"
#include "perception/services.h"
#include "perception/storage_manager.h"

using ::perception::DirectoryEntry;
using ::perception::GetService;
using ::perception::ReadDirectoryRequest;
using ::perception::StorageManager;

namespace perception {
namespace linux_syscalls {

long getdents64(unsigned int fd, dirent *dirp, unsigned int count) {
  auto descriptor = GetFileDescriptor(fd);
  if (!descriptor || descriptor->type != FileDescriptor::DIRECTORY ||
      count == 0 || descriptor->directory.finished_iterating) {
    return 0;
  }

  ReadDirectoryRequest request;
  request.path = descriptor->directory.name;
  request.first_index = descriptor->directory.iterating_offset;
  request.maximum_number_of_entries = count / sizeof(dirent);

  auto status_or_response = GetService<StorageManager>().ReadDirectory(request);

  if (!status_or_response) return 0;

  int return_index = 0;
  size_t return_size = 0;
  for (auto entry : status_or_response->entries) {
    if (return_index >= count / sizeof(dirent)) break;

    switch (entry.type) {
      case DirectoryEntry::Type::FILE:
        dirp[return_index].d_type = DT_REG;
        break;
      case DirectoryEntry::Type::DIRECTORY:
        dirp[return_index].d_type = DT_DIR;
        break;
      default:
        std::cout << "Unhandled DirectoryEntry::Type " << (int)entry.type
                  << " in Musl getdents64." << std::endl;
    }

    int chars_to_copy = entry.name.size();
    memcpy(dirp[return_index].d_name, entry.name.c_str(), chars_to_copy);
    memset(&dirp[return_index].d_name[chars_to_copy], 0, 256 - chars_to_copy);
    dirp[return_index].d_reclen = sizeof(dirent);
    dirp[return_index].d_off = sizeof(dirent) * (return_index + 1);

    return_index++;
    return_size += sizeof(dirent);
  }

  descriptor->directory.iterating_offset += return_index;
  descriptor->directory.finished_iterating =
      !status_or_response->has_more_entries;

  return return_size;
}

}  // namespace linux_syscalls
}  // namespace perception
