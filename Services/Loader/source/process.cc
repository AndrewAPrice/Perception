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
#pragma "process.h"

#include <iostream>
#include <memory>
#include <vector>

#include "elf_file.h"
#include "elf_file_cache.h"
#include "perception/processes.h"
#include "types.h"

using ::perception::NotifyUponProcessTermination;
using ::perception::ProcessId;

namespace {

// A map of process IDs to the ELF file dependencies.
std::map<ProcessId, std::vector<std::shared_ptr<ElfFile>>> pid_to_dependencies;

}  // namespace

void RecordChildPidAndDependencies(
    ::perception::ProcessId child_pid,
    const std::vector<std::shared_ptr<ElfFile>>& dependencies) {
  for (auto dependency : dependencies) dependency->IncrementInstances();

  pid_to_dependencies[child_pid] = dependencies;

  NotifyUponProcessTermination(child_pid, [child_pid]() {
    auto itr = pid_to_dependencies.find(child_pid);
    if (itr == pid_to_dependencies.end()) {
      std::cout << "Loader was listening for when process " << std::dec
                << child_pid
                << " exits, but doesn't have a record of the dependencies it "
                   "was using."
                << std::endl;
      return;
    }

    for (auto dependency : itr->second) DecrementElfFile(dependency);

    pid_to_dependencies.erase(itr);
  });
}
