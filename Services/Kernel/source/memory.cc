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

#include "physical_allocator.h"
#include "process.h"
#include "virtual_allocator.h"

#ifdef __TEST__
#include <stdio.h>
#else
extern "C" void memcpy(char *dest, const char *src, size_t count) {
  while (count > 0) {
    *dest = *src;
    dest++;
    src++;
    count--;
  }
}

extern "C" void memset(char *dest, char val, size_t count) {
  while (count > 0) {
    *dest = val;
    dest++;
    count--;
  }
}
#endif

bool IsKernelAddress(size_t address) {
  return address >= VIRTUAL_MEMORY_OFFSET;
}

bool CopyKernelMemoryIntoProcess(size_t from_start, size_t to_start,
                                 size_t to_end, Process *process) {
  VirtualAddressSpace &address_space = process->virtual_address_space;

  // The process's memory is mapped into pages. We'll copy page by page.
  size_t to_first_page = to_start & ~(PAGE_SIZE - 1);  // Round down.
  size_t to_last_page =
      (to_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  // Round up.

  size_t to_page = to_first_page;
  for (; to_page < to_last_page; to_page += PAGE_SIZE) {
    size_t physical_page_address =
        address_space.GetOrCreateVirtualPage(to_page);
    if (physical_page_address == OUT_OF_MEMORY) {
      // We ran out of memory trying to allocate the virtual page.
      return false;
    }

    size_t temp_addr =
        (size_t)TemporarilyMapPhysicalPages(physical_page_address, 5);

    // Indices where to start/finish copying within the page.
    size_t offset_in_page_to_start_copying_at =
        to_start > to_page ? to_start - to_page : 0;
    size_t offset_in_page_to_finish_copying_at =
        to_page + PAGE_SIZE > to_end ? to_end - to_page : PAGE_SIZE;
    size_t copy_length = offset_in_page_to_finish_copying_at -
                         offset_in_page_to_start_copying_at;

    memcpy((char *)(temp_addr + offset_in_page_to_start_copying_at),
           (char *)(from_start), copy_length);

    from_start += copy_length;
  }

  return true;
}

size_t PagesThatContainBytes(size_t bytes) {
  return (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
}
