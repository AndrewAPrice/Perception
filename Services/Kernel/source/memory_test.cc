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

#include "memory.h"
#include "process.h"
#include "virtual_allocator.h"
#include "testing.h"
#include "object_pools.h"
#include <unordered_map>

struct PhysicalPageBuffer {
  size_t entries[512];
};

extern std::unordered_map<size_t, PhysicalPageBuffer> simulated_ram;

TEST(MemoryPagesCalculationTest) {
  ASSERT(PagesThatContainBytes(0), (size_t)0);
  ASSERT(PagesThatContainBytes(1), (size_t)1);
  ASSERT(PagesThatContainBytes(4096), (size_t)1);
  ASSERT(PagesThatContainBytes(4097), (size_t)2);
}

TEST(MemoryIsKernelAddressTest) {
  ASSERT(IsKernelAddress(0x400000), false); // User address
  ASSERT(IsKernelAddress(VIRTUAL_MEMORY_OFFSET), true); // Kernel address start
  ASSERT(IsKernelAddress(0xFFFFFFFFFFFFFFFF), true); // Top of kernel address space
}

TEST(MemoryCopyKernelToProcessTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  Process* p1 = CreateProcess(false, false);
  ASSERT(p1 != nullptr, true);

  // Data in simulated kernel space
  const char* src_data = "Perception OS Paging is Awesome!";
  size_t copy_len = 33; // length including null-terminator

  // Copy kernel memory to process p1 user virtual address 0x800000
  size_t dest_virtual_addr = 0x800000;
  bool success = CopyKernelMemoryIntoProcess(
      (size_t)src_data, dest_virtual_addr, dest_virtual_addr + copy_len, p1);
  
  ASSERT(success, true);

  // Retrieve mapped physical address of 0x800000
  size_t phys_page = p1->virtual_address_space.GetPhysicalAddress(dest_virtual_addr, false);
  ASSERT(phys_page != OUT_OF_MEMORY, true);

  // Inspect the copied data inside the simulated RAM page buffer
  char* dest_data = (char*)simulated_ram[phys_page].entries;
  
  // Verify the copied string matches
  int match = 1;
  for (size_t i = 0; i < copy_len; i++) {
    if (dest_data[i] != src_data[i]) {
      match = 0;
      break;
    }
  }
  ASSERT(match, 1);

  // Clean up
  DestroyProcess(p1);
}
