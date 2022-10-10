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

#include "linux_syscalls/access.h"

#include <unistd.h>

#include <iostream>

#include "Permebuf.h"
#include "permebuf/Libraries/perception/storage_manager.permebuf.h"

using ::permebuf::perception::StorageManager;

namespace perception {
namespace linux_syscalls {

long access(const char *pathname, int mode) {
  Permebuf<StorageManager::CheckPermissionsRequest> request;
  request->SetPath(std::string(pathname));
  auto status_or_response =
      StorageManager::Get().CallCheckPermissions(std::move(request));
  if (status_or_response) {
    const auto& response = *status_or_response;
    if (((mode & F_OK) && !response.GetFileExists()) ||
    ((mode & R_OK) && !response.GetCanRead()) ||
    ((mode & W_OK) && !response.GetCanWrite()) ||
    ((mode & X_OK) && !response.GetCanExecute())) {
      return -1;
    }
    return 0;
  } else {
    errno = EINVAL;
    return -1;
  }
}

}  // namespace linux_syscalls
}  // namespace perception
