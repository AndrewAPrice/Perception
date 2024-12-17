// Copyright 2024 Google LLC
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

#include <memory>
#include <vector>

#include "elf_file.h"
#include "types.h"

// Record all of the ELF file dependencies for a child process. This keeps them
// in the cache while a program is running, so multiple instances of the same
// executable and shared libraries don't need to be reloaded from disk and can
// share the same instance of read-only memory.
void RecordChildPidAndDependencies(
    ::perception::ProcessId child_pid,
    const std::vector<std::shared_ptr<ElfFile>>& dependencies);
