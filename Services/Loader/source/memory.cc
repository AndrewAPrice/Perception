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

#include "memory.h"

#include <iostream>
#include <map>
#include <memory>

#include "memory.h"
#include "perception/memory.h"
#include "perception/processes.h"

using ::perception::AllocateMemoryPages;
using ::perception::kPageSize;
using ::perception::ProcessId;
using ::perception::ReleaseMemoryPages;
using ::perception::SetChildProcessMemoryPages;

namespace {

// Allocates any unallocated page ranges in [first_page, last_page) in contiguous batches.
bool AllocatePageRange(size_t first_page, size_t last_page,
                       std::map<size_t, void*>& child_memory_pages) {
  for (size_t page = first_page; page < last_page; page += kPageSize) {
    if (child_memory_pages.contains(page)) continue;

    size_t unallocated_run = 0;
    for (size_t next_page = page; next_page < last_page;
         next_page += kPageSize) {
      if (child_memory_pages.contains(next_page)) break;
      unallocated_run++;
    }

    void* allocated = AllocateMemoryPages(unallocated_run);
    if (allocated == nullptr) {
      std::cout << "Couldn't allocate memory to child pages." << std::endl;
      return false;
    }

    for (size_t i = 0; i < unallocated_run; i++)
      child_memory_pages[page + i * kPageSize] =
          (uint8*)allocated + i * kPageSize;

    page += (unallocated_run - 1) * kPageSize;
  }
  return true;
}

// Returns a pointer into the child page (allocating it memory if it doesn't yet
// exists), or nullptr if it couldn't be allocated.
void* GetChildPage(size_t page_address,
                   std::map<size_t, void*>& child_memory_pages) {
  auto itr = child_memory_pages.find(page_address);
  if (itr != child_memory_pages.end())
    return itr->second;  // Already allocated.

  // Allocate this memory page.
  void* memory = AllocateMemoryPages(/*pages=*/1);
  if (memory == nullptr) return nullptr;  // Couldn't allocate memory.

  child_memory_pages.insert({page_address, memory});
  return memory;
}

}  // namespace

// Copies data from the file into the process's memory.
bool CopyIntoMemory(const void* data, size_t size, size_t address,
                    std::map<size_t, void*>& child_memory_pages,
                    std::string_view name) {
  size_t address_end = address + size;

  size_t first_page = address & ~(kPageSize - 1);  // Round down.
  size_t last_page =
      (address_end + kPageSize - 1) & ~(kPageSize - 1);  // Round up.

  if (!AllocatePageRange(first_page, last_page, child_memory_pages))
    return false;

  size_t page = first_page;
  for (; page < last_page; page += kPageSize) {
    size_t memory = (size_t)child_memory_pages[page];

    // Indices where to start/finish clearing within the page.
    size_t offset_in_page_to_start_copying_at =
        address > page ? address - page : 0;
    size_t offset_in_page_to_finish_copying_at =
        page + kPageSize > address_end ? address_end - page : kPageSize;

    size_t copy_length = offset_in_page_to_finish_copying_at -
                         offset_in_page_to_start_copying_at;

    memcpy((unsigned char*)(memory + offset_in_page_to_start_copying_at), data,
           copy_length);

    data = (void*)((size_t)data + copy_length);
  }
  return true;
}

// Touches memory, making sure it is available, but doesn't copy anything into
// it.
bool LoadMemory(size_t address, size_t size,
                std::map<size_t, void*>& child_memory_pages) {
  size_t address_end = address + size;

  size_t first_page = address & ~(kPageSize - 1);  // Round down.
  size_t last_page =
      (address_end + kPageSize - 1) & ~(kPageSize - 1);  // Round up.

  if (!AllocatePageRange(first_page, last_page, child_memory_pages))
    return false;

  size_t page = first_page;
  for (; page < last_page; page += kPageSize) {
    size_t memory = (size_t)child_memory_pages[page];

    // Indices where to start/finish clearing within the page.
    size_t offset_in_page_to_start_copying_at =
        address > page ? address - page : 0;
    size_t offset_in_page_to_finish_copying_at =
        page + kPageSize > address_end ? address_end - page : kPageSize;

    size_t copy_length = offset_in_page_to_finish_copying_at -
                         offset_in_page_to_start_copying_at;
    memset((unsigned char*)(memory + offset_in_page_to_start_copying_at), 0,
           copy_length);
  }
  return true;
}

void FreeChildMemoryPages(std::map<size_t, void*>& child_memory_pages) {
  for (std::pair<size_t, void*> addr_and_memory : child_memory_pages)
    ReleaseMemoryPages(addr_and_memory.second, /*number=*/1);
}

void SendMemoryPagesToChild(ProcessId child_pid,
                            std::map<size_t, void*>& child_memory_pages) {
  if (child_memory_pages.empty()) return;

  auto it = child_memory_pages.begin();
  size_t run_dest = it->first;
  size_t run_src = (size_t)it->second;
  size_t run_count = 1;
  ++it;

  for (; it != child_memory_pages.end(); ++it) {
    size_t dest = it->first;
    size_t src = (size_t)it->second;

    if (dest == run_dest + run_count * kPageSize &&
        src == run_src + run_count * kPageSize) {
      run_count++;
    } else {
      SetChildProcessMemoryPages(child_pid, run_src, run_dest, run_count);
      run_dest = dest;
      run_src = src;
      run_count = 1;
    }
  }

  SetChildProcessMemoryPages(child_pid, run_src, run_dest, run_count);
}