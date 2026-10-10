// Copyright 2026 Google LLC
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

#pragma once

#include "types.h"

namespace perception {

// Auxiliary vector entry type for the standard input SharedMemoryPipe ID.
constexpr size_t kAuxvPerceptionStdin = 0x1000;

// Auxiliary vector entry type for the standard output SharedMemoryPipe ID.
constexpr size_t kAuxvPerceptionStdout = 0x1001;

// Auxiliary vector entry type for the standard error SharedMemoryPipe ID.
constexpr size_t kAuxvPerceptionStderr = 0x1002;

}  // namespace perception
