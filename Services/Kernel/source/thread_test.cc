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

#include "process.h"
#include "thread.h"
#include "virtual_allocator.h"
#include "testing.h"
#include "object_pools.h"

TEST(ThreadLifecycleAndReclamationTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  Process* p1 = CreateProcess(false, true);
  ASSERT(p1 != nullptr, true);

  // Create child process
  Process* child = CreateChildProcess(p1, (char*)"MyChild", 0);
  ASSERT(child != nullptr, true);
  ASSERT(IsProcessAChildOfParent(p1, child), true);
  ASSERT(child->parent, p1);

  // Create thread inside the child process
  Thread* t1 = CreateThread(child, 0x1000, 42);
  ASSERT(t1 != nullptr, true);
  ASSERT(child->thread_count, (unsigned short)1);
  ASSERT(GetThreadFromTid(child, t1->id), t1);

  // Destroy the last thread. This should automatically trigger child process destruction!
  DestroyThread(t1, /*process_being_destroyed=*/false);

  // Verify child process was automatically reclaimed
  ASSERT(GetProcessFromPid(child->pid) == nullptr, true);
  ASSERT(IsProcessAChildOfParent(p1, child), false);

  // Clean up parent
  DestroyProcess(p1);
}

TEST(ThreadAddressClearOnTerminationTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  Process* p1 = CreateProcess(false, true);
  ASSERT(p1 != nullptr, true);

  // Allocate a page in p1.
  size_t page_addr = p1->virtual_address_space.AllocatePages(1);
  ASSERT(page_addr != OUT_OF_MEMORY, true);

  // Write a non-zero value to the page.
  size_t offset = 16;
  size_t target_address = page_addr + offset;
  
  size_t physical_page = p1->virtual_address_space.GetPhysicalAddress(page_addr, false);
  ASSERT(physical_page != OUT_OF_MEMORY, true);
  
  volatile uint64* ptr = (volatile uint64*)((size_t)TemporarilyMapPhysicalPages(physical_page, 1) + offset);
  *ptr = 0xDEADBEEF;
  ASSERT(*ptr, (uint64)0xDEADBEEF);

  // Create a thread and set address_to_clear_on_termination.
  Thread* t1 = CreateThread(p1, 0x1000, 42);
  ASSERT(t1 != nullptr, true);
  t1->address_to_clear_on_termination = target_address;

  // Destroy the thread. This automatically destroys p1 as it is the last thread.
  DestroyThread(t1, /*process_being_destroyed=*/false);

  // Verify that the target address is now cleared to 0.
  ptr = (volatile uint64*)((size_t)TemporarilyMapPhysicalPages(physical_page, 1) + offset);
  ASSERT(*ptr, 0ULL);
}
