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

#include "shared_memory.h"
#include "process.h"
#include "virtual_allocator.h"
#include "object_pools.h"
#include "testing.h"

namespace {

Process* CreateTestProcess(const char* name) {
  Process* p = CreateProcess(false, false);
  return p;
}

}  // namespace

TEST(SharedMemoryCreationAndMappingTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();
  InitializeSharedMemory();

  Process* p1 = CreateTestProcess("Process1");
  ASSERT(p1 != nullptr, true);

  // Create and map 2 pages of shared memory into p1
  SharedMemoryInProcess* shm_p1 = CreateAndMapSharedMemoryBlockIntoProcess(
      p1, 2, SM_JOINERS_CAN_WRITE, 0);
  
  ASSERT(shm_p1 != nullptr, true);
  ASSERT(shm_p1->mapped_pages, (size_t)2);
  ASSERT(shm_p1->shared_memory->size_in_pages, (size_t)2);
  ASSERT(shm_p1->shared_memory->processes_referencing_this_block, (size_t)1);
  ASSERT(shm_p1->virtual_address != 0, true);

  // Create process p2 and join the same shared memory block
  Process* p2 = CreateTestProcess("Process2");
  ASSERT(p2 != nullptr, true);

  SharedMemoryInProcess* shm_p2 = JoinSharedMemory(p2, shm_p1->shared_memory->id);
  ASSERT(shm_p2 != nullptr, true);
  ASSERT(shm_p2->mapped_pages, (size_t)2);
  ASSERT(shm_p2->shared_memory->processes_referencing_this_block, (size_t)2);
  ASSERT(shm_p2->virtual_address != 0, true);

  // Leave shared memory in p2
  LeaveSharedMemory(p2, shm_p1->shared_memory->id);
  ASSERT(shm_p1->shared_memory->processes_referencing_this_block, (size_t)1);

  // Leave shared memory in p1
  LeaveSharedMemory(p1, shm_p1->shared_memory->id);
}

TEST(SharedMemoryGrowTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();
  InitializeSharedMemory();

  Process* p1 = CreateTestProcess("Process1");
  ASSERT(p1 != nullptr, true);

  SharedMemoryInProcess* shm_p1 = CreateAndMapSharedMemoryBlockIntoProcess(
      p1, 2, SM_JOINERS_CAN_WRITE, 0);
  
  ASSERT(shm_p1 != nullptr, true);
  ASSERT(shm_p1->mapped_pages, (size_t)2);

  // Grow shared memory block to 4 pages
  SharedMemoryInProcess* shm_p1_grown = GrowSharedMemory(p1, shm_p1->shared_memory->id, 4);
  ASSERT(shm_p1_grown != nullptr, true);
  ASSERT(shm_p1_grown->mapped_pages, (size_t)4);
  ASSERT(shm_p1_grown->shared_memory->size_in_pages, (size_t)4);

  // Clean up
  LeaveSharedMemory(p1, shm_p1_grown->shared_memory->id);
}
