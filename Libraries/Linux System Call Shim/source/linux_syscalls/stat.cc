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

#include "permebuf/Libraries/perception/storage_manager.permebuf.h"

using ::permebuf::perception::StorageManager;

namespace perception {
namespace linux_syscalls {

long stat(const char* pathname, struct stat* statbuf) {
  Permebuf<StorageManager::GetFileStatisticsRequest> request;
  request->SetPath(pathname);

  auto status_or_response =
      StorageManager::Get().CallGetFileStatistics(std::move(request));
  if (!status_or_response) {
    errno = EINVAL;
    return -1;
  }

  if (!status_or_response->GetExists()) {
    errno = ENOENT;
    return -1;
  }

  memset(statbuf, 0, sizeof(struct stat));
  if (status_or_response->GetIsDirectory())
    statbuf->st_mode = S_IFDIR;
  else if (status_or_response->GetIsFile()) {
    statbuf->st_mode = S_IFREG;
    statbuf->st_size = status_or_response->GetSizeInBytes();
  }

  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
