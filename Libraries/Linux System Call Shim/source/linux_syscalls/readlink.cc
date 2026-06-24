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

#include "linux_syscalls/readlink.h"

#include <errno.h>
#include <string.h>

#include <algorithm>

#include "perception/services.h"
#include "perception/storage_manager.h"

using ::perception::GetService;
using ::perception::StorageManager;

namespace perception {
namespace linux_syscalls {

long readlink(const char* pathname, char* buf, size_t bufsiz) {
  auto status_or_response = GetService<StorageManager>().ReadLink({pathname});
  if (status_or_response) {
    size_t chars_to_copy = std::min(status_or_response->path.size(), bufsiz);
    memcpy(buf, status_or_response->path.c_str(), chars_to_copy);
    return chars_to_copy;
  } else {
    if (status_or_response.Status() == Status::FILE_NOT_FOUND) {
      return -ENOENT;
    } else {
      return -EINVAL;
    }
  }
}

}  // namespace linux_syscalls
}  // namespace perception
