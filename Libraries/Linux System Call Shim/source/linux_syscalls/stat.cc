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

#include <iostream>

namespace perception {
namespace linux_syscalls {

long stat(const char* pathname, struct stat* statbuf) {
  std::cout << "System call stat unimplemented. Called for path: " << pathname
            << std::endl;
  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception
