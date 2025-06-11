// Copyright 2021 Google LLC
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

#include "aa_tree.h"
#include "liballoc.h"
#include "linked_list.h"
#include "messages.h"
#include "object_pool.h"
#include "physical_allocator.h"
#include "process.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_allocator.h"

namespace {

// The last assigned shared memory ID.
size_t last_assigned_shared_memory_id;

// Linked list of all shared memory blocks.
LinkedList<SharedMemory, &SharedMemory::all_shared_memories_node>
    all_shared_memories;

// Creates a shared memory block.
SharedMemory* CreateSharedMemoryBlock(
    Process* process, size_t pages, size_t flags,
    size_t message_id_for_lazily_loaded_pages) {
  SharedMemory* shared_memory = ObjectPool<SharedMemory>::Allocate();
  if (shared_memory == nullptr) {
    return nullptr;
  }

  last_assigned_shared_memory_id++;
  shared_memory->id = last_assigned_shared_memory_id;
  shared_memory->size_in_pages = pages;
  shared_memory->flags = flags;
  shared_memory->processes_referencing_this_block = 0;
  shared_memory->physical_pages = (size_t*)malloc(sizeof(size_t) * pages);

  if (shared_memory->physical_pages == nullptr) {
    // Out of memory.
    ObjectPool<SharedMemory>::Release(shared_memory);
    return nullptr;
  }

  for (size_t page = 0; page < pages; page++)
    shared_memory->physical_pages[page] = OUT_OF_PHYSICAL_PAGES;

  shared_memory->creator_pid = process->pid;
  shared_memory->pids_allowed_to_assign_memory_pages.Insert(process->pid);
  shared_memory->message_id_for_lazily_loaded_pages =
      message_id_for_lazily_loaded_pages;

  all_shared_memories.AddBack(shared_memory);

  if ((flags & SM_LAZILY_ALLOCATED) == 0) {
    // Not lazily allocated, so all pages should be
    // allocated now.
    for (size_t page = 0; page < pages; page++) {
      size_t physical_page = GetPhysicalPage();
      if (physical_page == OUT_OF_PHYSICAL_PAGES) {
        // Out of memory.
        ObjectPool<SharedMemory>::Release(shared_memory);
        return nullptr;
      }
      shared_memory->physical_pages[page] = physical_page;
    }
  }

  return shared_memory;
}

void MapSharedMemoryPageInEachProcess(SharedMemory* shared_memory,
                                      size_t page) {
  if (page >= shared_memory->size_in_pages)
    return;  // Beyond the end of the shared memory.

  // Map the page into each shared memory block.
  size_t physical_address = shared_memory->physical_pages[page];
  if (physical_address == OUT_OF_PHYSICAL_PAGES)
    return;  // No physical address is allocated to this page.

  size_t offset_of_page_in_bytes = page * PAGE_SIZE;

  for (auto* shared_memory_in_process : shared_memory->joined_processes) {
    Process* process = shared_memory_in_process->process;
    bool can_write = CanProcessWriteToSharedMemory(process, shared_memory);
    size_t virtual_address =
        shared_memory_in_process->virtual_address + offset_of_page_in_bytes;
    process->virtual_address_space.MapPhysicalPageAt(
        virtual_address, physical_address, false, can_write, false);
  }

  // Wake up each thread that was waiting for this page.
  for (ThreadWaitingForSharedMemoryPage* waiting_thread =
           shared_memory->waiting_threads.FirstItem();
       waiting_thread != nullptr;) {
    auto* next = shared_memory->waiting_threads.NextItem(waiting_thread);
    if (waiting_thread->page == page) {
      // This thread is waiting for this page.
      shared_memory->waiting_threads.Remove(waiting_thread);

      // Wake this thread.
      ScheduleThread(waiting_thread->thread);
      waiting_thread->thread->thread_is_waiting_for_shared_memory = nullptr;

      ObjectPool<ThreadWaitingForSharedMemoryPage>::Release(waiting_thread);
    }
    waiting_thread = next;
  }
}

SharedMemory* GetSharedMemoryFromId(size_t shared_memory_id) {
  // Search for the shared memory block with the matching ID.
  for (auto* shared_memory : all_shared_memories)
    if (shared_memory->id == shared_memory_id) return shared_memory;

  // Can't find any shared memory block with this ID.
  return nullptr;
}

void SleepThreadUntilSharedMemoryPageIsCreatedAndNotifyCreator(
    SharedMemory* shared_memory, size_t page, Process* creator) {
  if (page >= shared_memory->size_in_pages)
    return;  // Beyond the end of the shared memory.

  if (shared_memory->physical_pages[page] != OUT_OF_PHYSICAL_PAGES)
    return;  // The memory is already allocated. Nothing to wait for.

  auto waiting_thread =
      ObjectPool<ThreadWaitingForSharedMemoryPage>::Allocate();
  if (waiting_thread == nullptr) return;  // Out of memory.

  waiting_thread->thread = running_thread;
  waiting_thread->shared_memory = shared_memory;
  waiting_thread->page = page;

  shared_memory->waiting_threads.AddBack(waiting_thread);

  // Sleep the thread. It will be rewoken when the shared memory page is
  // allocated.
  UnscheduleThread(running_thread);

  // Notify the creator that someone wants this page.
  SendKernelMessageToProcess(creator,
                             shared_memory->message_id_for_lazily_loaded_pages,
                             page * PAGE_SIZE, 0, 0, 0, 0);
}

void MapPhysicalPageInSharedMemory(SharedMemory* shared_memory, size_t page,
                                   size_t physical_address) {
  size_t old_page = shared_memory->physical_pages[page];
  if (old_page == physical_address)
    return;  // Page is already mapped. Nothing to do.

  if (old_page != OUT_OF_PHYSICAL_PAGES) {
    FreePhysicalPage(old_page);  // Unallocate the existing physical page.

    // Unmap it in each process so we don't get an error trying to overwrite an
    // existing page table entry.
    size_t offset_of_page_in_bytes = page * PAGE_SIZE;
    for (auto* shared_memory_in_process : shared_memory->joined_processes) {
      Process* process = shared_memory_in_process->process;
      size_t virtual_address =
          shared_memory_in_process->virtual_address + offset_of_page_in_bytes;
      process->virtual_address_space.ReleasePages(virtual_address, 1);
    }
  }

  shared_memory->physical_pages[page] = physical_address;

  // Now each process needs to know about the shared memory page.
  MapSharedMemoryPageInEachProcess(shared_memory, page);
}

// Handles a page fault because the process tried to access an unallocated page
// in a shared memory block.
bool HandleSharedMessagePageFault(Process* process, SharedMemory* shared_memory,
                                  size_t page) {
  Process* creator = GetProcessFromPid(shared_memory->creator_pid);

  // Should we create the page?
  if (creator == nullptr || process == creator) {
    // Either the creator no longer exists, or this is the creator. The page
    // will be created.
    size_t physical_address = GetPhysicalPage();
    if (physical_address == OUT_OF_PHYSICAL_PAGES)
      return false;  // Out of memory.

    MapPhysicalPageInSharedMemory(shared_memory, page, physical_address);

  } else {
    // Not the creator. Message the creator and sleep this thread.
    SleepThreadUntilSharedMemoryPageIsCreatedAndNotifyCreator(shared_memory,
                                                              page, creator);
  }
  return true;
}

}  // namespace

// Initializes the internal structures for shared memory.
void InitializeSharedMemory() {
  last_assigned_shared_memory_id = 0;
  new (&all_shared_memories)
      LinkedList<SharedMemory, &SharedMemory::all_shared_memories_node>();
}

// Creates a shared memory block and map it into a procses.
SharedMemoryInProcess* CreateAndMapSharedMemoryBlockIntoProcess(
    Process* process, size_t pages, size_t flags,
    size_t message_id_for_lazily_loaded_pages) {
  // Create the shared memory block.
  SharedMemory* shared_memory = CreateSharedMemoryBlock(
      process, pages, flags, message_id_for_lazily_loaded_pages);
  if (shared_memory == nullptr) {
    // Could not create shared memory.
    return nullptr;
  }

  // Map it into this process.
  SharedMemoryInProcess* shared_memory_in_process =
      MapSharedMemoryIntoProcess(process, shared_memory);
  if (shared_memory_in_process == nullptr) {
    ObjectPool<SharedMemory>::Release(shared_memory);
  }
  return shared_memory_in_process;
}

// Releases a shared memory block. Please make sure that there are no
// processes referencing the shared memory block before calling this.
void ReleaseSharedMemoryBlock(SharedMemory* shared_memory) {
  if (shared_memory->processes_referencing_this_block > 0) {
    // This should never be triggered.
    print << "Attempting to release shared memory that still is being "
             "referenced by a process.\n";
    return;
  }
  if (!shared_memory->waiting_threads.IsEmpty()) {
    // This should never be triggered.
    print << "Attempting to release shared memory that still is blocking "
             "other threads.\n";
    return;
  }

  // Release each physical page associated with this shared memory block.)
  for (size_t page = 0; page < shared_memory->size_in_pages; page++) {
    if (shared_memory->physical_pages[page] != OUT_OF_PHYSICAL_PAGES) {
      // Release this physical page.
      FreePhysicalPage(shared_memory->physical_pages[page]);
    }
  }
  free(shared_memory->physical_pages);

  // Remove us from the linked list of shared memory.
  all_shared_memories.Remove(shared_memory);

  // Release the SharedMemory object.
  ObjectPool<SharedMemory>::Release(shared_memory);
}

// Joins a shared memory block. Ensures that a shared memory is only mapped once
// per process. Returns the virtual address of the shared memory block or 0.
SharedMemoryInProcess* JoinSharedMemory(Process* process,
                                        size_t shared_memory_id) {
  // See if this shared memory is already mapped into this process.
  for (auto* shared_memory_in_process : process->joined_shared_memories) {
    if (shared_memory_in_process->shared_memory->id == shared_memory_id) {
      // This shared memory is already mapped into the process.
      shared_memory_in_process->references++;
      return shared_memory_in_process;
    }
  }

  // The shared memory is not mapped to the process, so we'll try to find it.
  SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
  if (shared_memory == nullptr) {
    // No shared memory with this ID exists.
    return 0;
  }

  // Map this shared memory in this process.
  return MapSharedMemoryIntoProcess(process, shared_memory);
}

bool JoinChildProcessInSharedMemory(Process* parent, Process* child,
                                    size_t shared_memory_id,
                                    size_t starting_address) {
  if (!IsProcessAChildOfParent(parent, child)) return false;

  SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
  if (shared_memory == nullptr) {
    // No shared memory with this ID exists.
    return false;
  }

  if (!IsPageAlignedAddress(starting_address)) {
    print << "JoinChildProcessInSharedMemory called with non page aligned "
             "address: "
          << NumberFormat::Hexidecimal << starting_address << '\n';
    starting_address = RoundDownToPageAlignedAddress(starting_address);
  }

  if (!child->virtual_address_space.ReserveAddressRange(
          starting_address, shared_memory->size_in_pages)) {
    // Can't reserve this address range. It is likely occupied.
    return false;
  }

  return MapSharedMemoryIntoProcessAtAddress(child, shared_memory,
                                             starting_address) != nullptr;
}

// Leaves a shared memory block, but doesn't unmap it if there are still other
// referenes to the shared memory block in the process.
void LeaveSharedMemory(Process* process, size_t shared_memory_id) {
  // Find the shared memory.
  for (auto* shared_memory_in_process : process->joined_shared_memories) {
    if (shared_memory_in_process->shared_memory->id == shared_memory_id) {
      // Found the shared memory block.
      shared_memory_in_process->references--;

      if (shared_memory_in_process->references == 0)
        UnmapSharedMemoryFromProcess(shared_memory_in_process);
      return;
    }
  }
}

void MovePageIntoSharedMemory(Process* process, size_t shared_memory_id,
                              size_t offset_in_buffer, size_t page_address) {
  if (process == nullptr) return;

  size_t physical_address =
      process->virtual_address_space.GetPhysicalAddress(page_address, true);
  if (physical_address == OUT_OF_MEMORY)
    return;  // This page doesn't exist or we don't own it. We can't move
             // it.
  process->virtual_address_space.ReleasePages(page_address, 1);

  // If we fail at any point we need to return the physical address.

  SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
  if (shared_memory == nullptr) {
    // Unknown shared memory ID.
    FreePhysicalPage(physical_address);
    return;
  }

  // if (!shared_memory->pids_allowed_to_assign_memory_pages.Contains(
  //       process->pid)) {
  //  Only the creator of the shared memory can move pages into shared memory.
  // FreePhysicalPage(physical_address);
  // return;
  //}

  // Work out where we are moving this page in the shared memory.
  size_t page = offset_in_buffer / PAGE_SIZE;
  if (page >= shared_memory->size_in_pages) {
    // Beyond the end of the shared memory.
    FreePhysicalPage(physical_address);
    return;
  }

  MapPhysicalPageInSharedMemory(shared_memory, page, physical_address);
}

bool MaybeHandleSharedMessagePageFault(size_t address) {
  if (running_thread == nullptr) {
    // This exception occured in the kernel.
    return false;
  }

  // Round address down to the page it's in.
  address &= ~(PAGE_SIZE - 1);

  Process* process = running_thread->process;

  // Loop through each shared memory in process.
  for (SharedMemoryInProcess* shared_memory_in_process :
       process->joined_shared_memories) {
    // Does this address fall within the shared memory block?
    if (address < shared_memory_in_process->virtual_address)
      continue;  // Address is too low.

    size_t page_in_shared_memory =
        (address - shared_memory_in_process->virtual_address) / PAGE_SIZE;

    SharedMemory* shared_memory = shared_memory_in_process->shared_memory;
    if (page_in_shared_memory >= shared_memory->size_in_pages)
      continue;  // Address is too high.

    // The address falls within this shared memory block.

    if ((shared_memory->flags & SM_LAZILY_ALLOCATED) == 0)
      return false;  // This shared memory block isn't lazily allocated.

    if (shared_memory->physical_pages[page_in_shared_memory] ==
        OUT_OF_PHYSICAL_PAGES) {
      // The page fault is because this page isn't allocated.
      return HandleSharedMessagePageFault(process, shared_memory,
                                          page_in_shared_memory);
    }

    // The page fault isn't because this page isn't allocated.
    return false;
  }

  // This address doesn't fall within shared memory.
  return false;
}

bool IsAddressAllocatedInSharedMemory(size_t shared_memory_id,
                                      size_t offset_in_shared_memory) {
  return GetPhysicalAddressOfPageInSharedMemory(shared_memory_id,
                                                offset_in_shared_memory) !=
         OUT_OF_PHYSICAL_PAGES;
}

size_t GetPhysicalAddressOfPageInSharedMemory(size_t shared_memory_id,
                                              size_t offset_in_shared_memory) {
  SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
  if (shared_memory == nullptr) return OUT_OF_PHYSICAL_PAGES;

  size_t page_in_shared_memory = offset_in_shared_memory / PAGE_SIZE;
  if (page_in_shared_memory >= shared_memory->size_in_pages)
    return OUT_OF_PHYSICAL_PAGES;  // Address is far too high.

  // Check that the page has a physical page allocated to it.
  return shared_memory->physical_pages[page_in_shared_memory];
}

void GrantPermissionToAllocateIntoSharedMemory(Process* grantor,
                                               size_t shared_memory_id,
                                               size_t grantee_pid) {
  SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
  if (shared_memory == nullptr) return;
  if (!shared_memory->pids_allowed_to_assign_memory_pages.Contains(
          grantor->pid))
    return;  // The grantor isn't allowed itself.

  shared_memory->pids_allowed_to_assign_memory_pages.Insert(grantee_pid);
}

bool CanProcessWriteToSharedMemory(Process* process,
                                   SharedMemory* shared_memory) {
  // Either the shared memory is writable by everyone, or this process is the
  // creator of the shared memory.
  return ((shared_memory->flags & SM_JOINERS_CAN_WRITE) != 0) ||
         shared_memory->creator_pid == process->pid;
}

// Gets information about a shared memory buffer as it pertains to a processes.
void GetSharedMemoryDetailsPertainingToProcess(Process* process,
                                               size_t shared_memory_id,
                                               size_t& flags,
                                               size_t& size_in_bytes) {
  SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
  if (shared_memory == nullptr) {
    flags = 0;
    size_in_bytes = 0;
    return;
  }

  flags = SMD_EXISTS;
  if (((shared_memory->flags & SM_JOINERS_CAN_WRITE) != 0) ||
      shared_memory->creator_pid == process->pid) {
    flags |= SMD_CAN_PROCESS_WRITE;
  }

  if ((shared_memory->flags & SM_LAZILY_ALLOCATED) != 0) {
    flags |= SMD_LAZILY_ALLOCATED;
  }

  // if (shared_memory->pids_allowed_to_assign_memory_pages.Contains(
  //     process->pid)) {
  flags |= SMD_CAN_PROCESS_ASSIGN_PAGES;
  // }

  size_in_bytes = shared_memory->size_in_pages * PAGE_SIZE;
}
