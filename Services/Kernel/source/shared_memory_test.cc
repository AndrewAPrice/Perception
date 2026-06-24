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

#include "messages.h"
#include "object_pools.h"
#include "process.h"
#include "shared_memory_event.h"
#include "testing.h"
#include "virtual_allocator.h"

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

TEST(SharedMemoryEventsTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();
  InitializeSharedMemory();

  Process* p1 = CreateTestProcess("Process1");
  Process* p2 = CreateTestProcess("Process2");
  ASSERT(p1 != nullptr, true);
  ASSERT(p2 != nullptr, true);

  // Create shared memory block in p1
  SharedMemoryInProcess* shm_p1 =
      CreateAndMapSharedMemoryBlockIntoProcess(p1, 2, SM_JOINERS_CAN_WRITE, 0);
  ASSERT(shm_p1 != nullptr, true);
  SharedMemory* shm = shm_p1->shared_memory;

  // 1. Register an event for p2
  RegisterSharedMemoryEvent(p2, shm->id, 0x100, 999);

  // Check event exists in both lists
  ASSERT(shm->events.IsEmpty(), false);
  ASSERT(p2->shared_memory_events.IsEmpty(), false);

  SharedMemoryEvent* ev1 = shm->events.FirstItem();
  ASSERT(ev1 != nullptr, true);
  ASSERT(ev1->process, p2);
  ASSERT(ev1->shared_memory, shm);
  ASSERT(ev1->offset, (size_t)0x100);
  ASSERT(ev1->message_id, (size_t)999);

  SharedMemoryEvent* ev2 = p2->shared_memory_events.FirstItem();
  ASSERT(ev1, ev2);  // should be the exact same event object

  // 2. Registering same event with different message ID updates it
  RegisterSharedMemoryEvent(p2, shm->id, 0x100, 111);
  ASSERT(ev1->message_id, (size_t)111);
  ASSERT(shm->events.FirstItem() == shm->events.LastItem(),
         true);  // still only 1 event

  // 3. Trigger the event
  TriggerSharedMemoryEvent(shm->id, 0x100);

  // Verify event is removed from both lists
  ASSERT(shm->events.IsEmpty(), true);
  ASSERT(p2->shared_memory_events.IsEmpty(), true);

  // Verify p2 received the message
  ASSERT(p2->messages_queued, (size_t)1);
  Message* msg = GetNextQueuedMessage(p2);
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)111);

  // 4. Register and then manually unregister an event
  RegisterSharedMemoryEvent(p2, shm->id, 0x200, 222);
  ASSERT(shm->events.IsEmpty(), false);
  ASSERT(p2->shared_memory_events.IsEmpty(), false);

  UnregisterSharedMemoryEvent(p2, shm->id, 0x200);
  ASSERT(shm->events.IsEmpty(), true);
  ASSERT(p2->shared_memory_events.IsEmpty(), true);

  // 5. Register multiple events and unregister all for process
  RegisterSharedMemoryEvent(p2, shm->id, 0x300, 333);
  RegisterSharedMemoryEvent(p2, shm->id, 0x400, 444);
  ASSERT(p2->shared_memory_events.IsEmpty(), false);

  UnregisterAllSharedMemoryEventsForProcess(p2);
  ASSERT(shm->events.IsEmpty(), true);
  ASSERT(p2->shared_memory_events.IsEmpty(), true);

  // Clean up
  LeaveSharedMemory(p1, shm->id);
}

TEST(SharedMemoryManualUnallocationTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();
  InitializeSharedMemory();

  Process* proc = CreateProcess(false, false);
  ASSERT(proc != nullptr, true);
  proc->virtual_address_space.SwitchToAddressSpace();

  SharedMemoryInProcess* sm_in_proc =
      CreateAndMapSharedMemoryBlockIntoProcess(proc, 2, 0, 0);
  ASSERT(sm_in_proc != nullptr, true);

  size_t vaddr = sm_in_proc->virtual_address;
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)2);

  // Attempt to manually unallocate the first page of shared memory
  proc->virtual_address_space.FreePages(vaddr, 1);

  // Verify shared page count did NOT decrease (UnmapVirtualPage did nothing)
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)2);
}
