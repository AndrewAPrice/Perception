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

#include "linux_syscalls/stat.h"

#include <sys/stat.h>

#include "perception/services.h"
#include "perception/storage_manager.h"

namespace perception {
namespace linux_syscalls {

long stat(const char* pathname, struct stat* statbuf) {
  auto status_or_response =
      GetService<StorageManager>().GetFileStatistics({pathname});
  if (!status_or_response) {
    errno = EINVAL;
    return -1;
  }

  if (!status_or_response->exists) {
    errno = ENOENT;
    return -1;
  }

  memset(statbuf, 0, sizeof(struct stat));
  switch (status_or_response->type) {
    case DirectoryEntry::Type::DIRECTORY:
      statbuf->st_mode = S_IFDIR;
      break;
    case DirectoryEntry::Type::FILE:
      statbuf->st_mode = S_IFREG;
      statbuf->st_size = status_or_response->size_in_bytes;
  }

  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
