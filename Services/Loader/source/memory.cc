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
#include "perception/shared_memory.h"

using ::perception::AllocateMemoryPages;
using ::perception::kPageSize;
using ::perception::ProcessId;
using ::perception::ReleaseMemoryPages;
using ::perception::SetChildProcessMemoryPage;
using ::perception::SharedMemory;

namespace {

// Turns a set of pages into a shared memory block.
std::shared_ptr<perception::SharedMemory> TurnPagesIntoSharedMemoryBlock(
    std::map<size_t, void*>& child_memory_pages, size_t first_page,
    size_t last_page) {
  size_t size = last_page - first_page + kPageSize;

  std::weak_ptr<SharedMemory> weak_shared_memory;

  std::shared_ptr<SharedMemory> shared_memory = SharedMemory::FromSize(
      size, SharedMemory::kLazilyAllocated,
      [&weak_shared_memory](size_t offset_of_page) {
        // Should never get called. Assign this a blank page.
        auto strong_shared_memory = weak_shared_memory.lock();
        if (strong_shared_memory)
          strong_shared_memory->AssignPage(AllocateMemoryPages(1),
                                           offset_of_page);
      });
  weak_shared_memory = shared_memory;

  for (size_t page = first_page; page <= last_page; page++) {
    auto itr = child_memory_pages.find(page);
    if (itr == child_memory_pages.end()) continue;  // Should never happen.

    size_t offset = page - first_page;
    shared_memory->AssignPage(itr->second, offset);
  }

  return shared_memory;
}

}  // namespace

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

// Copies data from the file into the process's memory.
bool CopyIntoMemory(const void* data, size_t size, size_t address,
                    std::map<size_t, void*>& child_memory_pages) {
  size_t address_end = address + size;

  size_t first_page = address & ~(kPageSize - 1);  // Round down.
  size_t last_page =
      (address_end + kPageSize - 1) & ~(kPageSize - 1);  // Round up.

  size_t page = first_page;
  for (; page < last_page; page += kPageSize) {
    size_t memory = (size_t)GetChildPage(page, child_memory_pages);
    if (memory == (size_t)nullptr) {
      std::cout << "Couldn't allocate memory to child page." << std::endl;
      return false;
    }

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

  size_t page = first_page;
  for (; page < last_page; page += kPageSize) {
    size_t memory = (size_t)GetChildPage(page, child_memory_pages);
    if (memory == (size_t)nullptr) {
      std::cout << "Couldn't allocate memory to child page." << std::endl;
      return false;
    }

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
  for (std::pair<size_t, void*> addr_and_memory : child_memory_pages) {
    SetChildProcessMemoryPage(child_pid, (size_t)addr_and_memory.second,
                              addr_and_memory.first);
  }
}

std::map<size_t, std::shared_ptr<SharedMemory>>
ConvertMapOfPagesIntoReadOnlySharedMemoryBlocks(
    std::map<size_t, void*>& child_memory_pages) {
  std::map<size_t, std::shared_ptr<SharedMemory>> shared_memory_blocks;

  bool has_pages = false;
  size_t first_page, last_page = 0;

  for (std::pair<size_t, void*> addr_and_memory : child_memory_pages) {
    size_t page_address = addr_and_memory.first;

    if (has_pages) {
      if (page_address == last_page + kPageSize) {
        // A contiguous page that can be part of the same shared memory block.
        last_page = page_address;
      } else {
        // A non-contiguous page. Turn the current first_page->last_page into a
        // shared memory block.
        shared_memory_blocks[first_page] = TurnPagesIntoSharedMemoryBlock(
            child_memory_pages, first_page, last_page);
        // Start a new block at this address.
        first_page = last_page = page_address;
      }
    } else {
      has_pages = true;
      first_page = last_page = page_address;
    }
  }

  if (has_pages) {
    shared_memory_blocks[first_page] = TurnPagesIntoSharedMemoryBlock(
        child_memory_pages, first_page, last_page);
  }

  return shared_memory_blocks;
}