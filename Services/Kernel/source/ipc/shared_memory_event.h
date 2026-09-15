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

#include "containers/linked_list.h"
#include "types.h"

namespace processes {
struct Process;
}

namespace ipc {

struct SharedMemory;

// Represents a registered shared memory event.
struct SharedMemoryEvent {
  processes::Process* process;
  SharedMemory* shared_memory;
  size_t offset;
  size_t message_id;
  containers::LinkedListNode node_in_process;
  containers::LinkedListNode node_in_shared_memory;
};

}  // namespace ipc

