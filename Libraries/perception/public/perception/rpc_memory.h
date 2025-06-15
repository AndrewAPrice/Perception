// Copyright 2025 Google LLC
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

#include <types.h>

#include <memory>

#include "perception/shared_memory.h"

namespace perception {

// Returns the memory buffer for sending to a process. This memory buffer must
// then be sent, because it sets the first byte to '1' and subsequent calls will
// sleep until it's '0' again (or the process disappears).
std::shared_ptr<SharedMemory> GetMemoryBufferForSendingToProcess(
    ProcessId process_id);

// Returns the memory buffer sent here from a receiving process. Keeps it mapped
// into memory (for fast reuse) until the process disappears.
std::shared_ptr<SharedMemory> GetMemoryBufferForReceivingFromProcess(
    ProcessId process_id, size_t shared_memory_id);

}  // namespace perception