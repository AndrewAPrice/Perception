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

#include <map>
#include <memory>

#include "perception/shared_memory.h"
#include "types.h"

// Copies data from the file into the process's memory.
bool CopyIntoMemory(const void* data, size_t size, size_t address,
                    std::map<size_t, void*>& child_memory_pages);

// Touches memory, making sure it is available, but doesn't copy anything into
// it.
bool LoadMemory(size_t address, size_t size,
                std::map<size_t, void*>& child_memory_pages);

// Frees the child memory pages. Used in flows where the child isn't
// successfully created.
void FreeChildMemoryPages(std::map<size_t, void*>& child_memory_pages);

// Sends the memory pages to the child.
void SendMemoryPagesToChild(::perception::ProcessId child_pid,
                            std::map<size_t, void*>& child_memory_pages);

// Converts a map of pages into a block of read-only shared memory.
std::map<size_t, std::shared_ptr<perception::SharedMemory>>
ConvertMapOfPagesIntoReadOnlySharedMemoryBlocks(
    std::map<size_t, void*>& child_memory_pages);
