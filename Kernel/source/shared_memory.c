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

#include "liballoc.h"
#include "messages.h"
#include "object_pools.h"
#include "physical_allocator.h"
#include "process.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_allocator.h"

// The last assigned shared memory ID.
size_t last_assigned_shared_memory_id;

// Linked list of all shared memory blocks.
struct SharedMemory* first_shared_memory;

// Initializes the internal structures for shared memory.
void InitializeSharedMemory() {
  last_assigned_shared_memory_id = 0;
  first_shared_memory = NULL;
}

// Creates a shared memory block.
struct SharedMemory* CreateSharedMemoryBlock(
    struct Process* process, size_t pages, size_t flags,
    size_t message_id_for_lazily_loaded_pages) {
  struct SharedMemory* shared_memory = AllocateSharedMemory();
  if (shared_memory == NULL) {
    return NULL;
  }

  last_assigned_shared_memory_id++;
  shared_memory->id = last_assigned_shared_memory_id;
  shared_memory->size_in_pages = pages;
  shared_memory->flags = flags;
  shared_memory->processes_referencing_this_block = 0;
  shared_memory->physical_pages = malloc(sizeof(size_t) * pages);

  if (shared_memory->physical_pages == NULL) {
    // Out of memory.
    ReleaseSharedMemory(shared_memory);
    return NULL;
  }

  for (size_t page = 0; page < pages; page++)
    shared_memory->physical_pages[page] = OUT_OF_PHYSICAL_PAGES;

  shared_memory->creator_pid = process->pid;
  shared_memory->message_id_for_lazily_loaded_pages =
      message_id_for_lazily_loaded_pages;
  shared_memory->next = NULL;
  shared_memory->previous = NULL;
  shared_memory->first_waiting_thread = NULL;
  shared_memory->first_process = NULL;

  if (first_shared_memory != NULL) {
    first_shared_memory->previous = shared_memory;
    shared_memory->next = first_shared_memory;
  }
  first_shared_memory = shared_memory;

  if ((flags & SM_LAZILY_ALLOCATED) == 0) {
    // We're not lazily allocated, so we should allocate all of
    // the pages now.
    for (size_t page = 0; page < pages; page++) {
      size_t physical_page = GetPhysicalPage();
      if (physical_page == OUT_OF_PHYSICAL_PAGES) {
        // Out of memory.
        ReleaseSharedMemoryBlock(shared_memory);
        return NULL;
      }
      shared_memory->physical_pages[page] = physical_page;
    }
  }

  return shared_memory;
}

// Creates a shared memory block and map it into a procses.
struct SharedMemoryInProcess* CreateAndMapSharedMemoryBlockIntoProcess(
    struct Process* process, size_t pages, size_t flags,
    size_t message_id_for_lazily_loaded_pages) {
  // Create the shared memory block.
  struct SharedMemory* shared_memory = CreateSharedMemoryBlock(
      process, pages, flags, message_id_for_lazily_loaded_pages);
  if (shared_memory == NULL) {
    // Could not create shared memory.
    return NULL;
  }

  // Map it into this process.
  struct SharedMemoryInProcess* shared_memory_in_process =
      MapSharedMemoryIntoProcess(process, shared_memory);
  if (shared_memory_in_process == NULL) {
    ReleaseSharedMemoryBlock(shared_memory);
  }
  return shared_memory_in_process;
}

// Releases a shared memory block. Please make sure that there are no
// processes referencing the shared memory block before calling this.
void ReleaseSharedMemoryBlock(struct SharedMemory* shared_memory) {
  if (shared_memory->processes_referencing_this_block > 0) {
    // This should never be triggered.
    PrintString(
        "Attempting to release shared memory that still is being "
        "referenced by a process.\n");
    return;
  }
  if (shared_memory->first_waiting_thread != NULL) {
    // This should never be triggered.
    PrintString(
        "Attempting to release shared memory that still is blocking "
        "other threads.\n");
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
  if (shared_memory->next != NULL) {
    shared_memory->next->previous = shared_memory->previous;
  }

  if (shared_memory->previous == NULL) {
    first_shared_memory = shared_memory->next;
  } else {
    shared_memory->previous->next = shared_memory->next;
  }

  // Release the SharedMemory object.
  ReleaseSharedMemory(shared_memory);
}

struct SharedMemory* GetSharedMemoryFromId(size_t shared_memory_id) {
  struct SharedMemory* shared_memory = first_shared_memory;
  while (shared_memory != NULL) {
    if (shared_memory->id == shared_memory_id) {
      // Found a shared memory block that matches the ID.
      return shared_memory;
    }
    shared_memory = shared_memory->next;
  }

  // Can't find any shared memory block with this ID.
  return NULL;
}

// Joins a shared memory block. Ensures that a shared memory is only mapped once
// per process. Returns the virtual address of the shared memory block or 0.
struct SharedMemoryInProcess* JoinSharedMemory(struct Process* process,
                                               size_t shared_memory_id) {
  // See if this shared memory is already mapped into this process.
  struct SharedMemoryInProcess* shared_memory_in_process =
      process->shared_memory;
  while (shared_memory_in_process != NULL) {
    if (shared_memory_in_process->shared_memory->id == shared_memory_id) {
      // This shared memory is already mapped into the process.
      shared_memory_in_process->references++;
      return shared_memory_in_process;
    }
    shared_memory_in_process = shared_memory_in_process->next_in_process;
  }

  // The shared memory is not mapped to the process, so we'll try to find it.
  struct SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
  if (shared_memory == NULL) {
    // No shared memory with this ID exists.
    return 0;
  }

  // Map this shared memory in this process.
  return MapSharedMemoryIntoProcess(process, shared_memory);
}

// Leaves a shared memory block, but doesn't unmap it if there are still other
// referenes to the shared memory block in the process.
void LeaveSharedMemory(struct Process* process, size_t shared_memory_id) {
  // Find the shared memory.
  struct SharedMemoryInProcess* shared_memory_in_process =
      process->shared_memory;
  while (shared_memory_in_process != NULL) {
    if (shared_memory_in_process->shared_memory->id == shared_memory_id) {
      // Found the shared memory block.
      shared_memory_in_process->references--;

      if (shared_memory_in_process == 0) {
        // No more references to this shared memory, so we can unmap
        // it.
        UnmapSharedMemoryFromProcess(process, shared_memory_in_process);
      }

      return;
    }
    shared_memory_in_process = shared_memory_in_process->next_in_process;
  }
}

void MovePageIntoSharedMemory(struct Process* process, size_t shared_memory_id,
                              size_t offset_in_buffer, size_t page_address) {
  if (process == NULL) return;

  size_t physical_address =
      GetPhysicalAddress(process->pml4, page_address, true);
  if (physical_address == OUT_OF_MEMORY)
    return false;  // This page doesn't exist or we don't own it. We can't move
                   // it.

  ReleaseVirtualMemoryInAddressSpace(process->pml4, page_address, 1, false);

  // If we fail at any point we need to return the physical address.

  struct SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
  if (shared_memory == NULL) {
    // Unknown shared memory ID.
    FreePhysicalPage(physical_address);
    return;
  }

  if (shared_memory->creator_pid != process->pid) {
    // Only the creator of the shared memory can move pages into shared memory.
    FreePhysicalPage(physical_address);
    return;
  }

  // Work out where we are moving this page in the shared memory.
  size_t page = page_address / PAGE_SIZE;
  if (page >= shared_memory->size_in_pages) {
    // Beyond the end of the shared memory.
    FreePhysicalPage(physical_address);
    return;
  }

  if (shared_memory->physical_pages[page] != OUT_OF_PHYSICAL_PAGES) {
    // There's already a physical page allocated here, so we need to release it.
    FreePhysicalPage(shared_memory->physical_pages[page]);
  }

  // Move this page into the shared memory, and map it into each process.
  shared_memory->physical_pages[page] = physical_address;
  MapSharedMemoryPageInEachProcess(shared_memory, page);
}

void SleepThreadUntilSharedMemoryPageIsCreatedAndNotifyCreator(
    struct SharedMemory* shared_memory, size_t page, struct Process* creator) {
  if (page >= shared_memory->size_in_pages)
    return;  // Beyond the end of the shared memory.

  if (shared_memory->physical_pages[page] != OUT_OF_PHYSICAL_PAGES)
    return;  // The memory is already allocated. Nothing to wait for.

  struct ThreadWaitingForSharedMemoryPage* waiting_thread =
      AllocateThreadWaitingForSharedMemoryPage();
  if (waiting_thread == NULL) return;  // Out of memory.

  waiting_thread->thread = running_thread;
  waiting_thread->shared_memory = shared_memory;
  waiting_thread->page = page;

  waiting_thread->next = shared_memory->first_waiting_thread;
  if (waiting_thread->next != NULL) {
    waiting_thread->next->previous = waiting_thread;
  }
  waiting_thread->previous = NULL;
  shared_memory->first_waiting_thread = waiting_thread;

  // Sleep the thread. It will be rewoken when the shared memory page is
  // allocated.
  UnscheduleThread(running_thread);

  // Notify the creator that someone wants this page.
  SendKernelMessageToProcess(creator,
                             shared_memory->message_id_for_lazily_loaded_pages,
                             page * PAGE_SIZE, 0, 0, 0, 0);
}

void MapSharedMemoryPageInEachProcess(struct SharedMemory* shared_memory,
                                      size_t page) {
  if (page >= shared_memory->size_in_pages)
    return;  // Beyond the end of the shared memory.

  // Map the page into each shared memory block.
  size_t physical_address = shared_memory->physical_pages[page];
  if (physical_address == OUT_OF_PHYSICAL_PAGES)
    return;  // No physical address is allocated to this page.

  size_t offset_of_page_in_bytes = page * PAGE_SIZE;

  for (struct SharedMemoryInProcess* shared_memory_in_process =
           shared_memory->first_process;
       shared_memory_in_process != NULL;
       shared_memory_in_process =
           shared_memory_in_process->next_in_shared_memory) {
    struct Process* process = shared_memory_in_process->process;
    bool can_write = CanProcessWriteToSharedMemory(process, shared_memory);
    size_t virtual_address =
        shared_memory_in_process->virtual_address + offset_of_page_in_bytes;
    MapPhysicalPageToVirtualPage(process->pml4, virtual_address,
                                 physical_address, false, can_write, false);
  }

  // Wake up each thread that was waiting for this page.
  for (struct ThreadWaitingForSharedMemoryPage* waiting_thread =
           shared_memory->first_waiting_thread;
       waiting_thread != NULL;) {
    if (waiting_thread->page == page) {
      // This thread is waiting for this page.

      // Wake this thread.
      ScheduleThread(waiting_thread->thread);
      waiting_thread->thread->thread_is_waiting_for_shared_memory = NULL;

      // Remove it from the linked list of waiting threads for this shared
      // memory.
      if (waiting_thread->previous == NULL) {
        shared_memory->first_waiting_thread = waiting_thread->next;
      } else {
        waiting_thread->previous->next = waiting_thread->next;
      }

      if (waiting_thread->next != NULL) {
        waiting_thread->next->previous = waiting_thread->previous;
      }

      struct ThreadWaitingForSharedMemoryPage* next = waiting_thread->next;
      ReleaseThreadWaitingForSharedMemoryPage(waiting_thread);
      waiting_thread = next;
    } else {
      // This thread is waiting for another page.
      waiting_thread = waiting_thread->next;
    }
  }
}

void MapPhysicalPageInSharedMemory(struct SharedMemory* shared_memory,
                                   size_t page, size_t physical_address) {
  size_t old_page = shared_memory->physical_pages[page];
  if (old_page == physical_address)
    return;  // Page is already mapped. Nothing to do.

  if (old_page != OUT_OF_PHYSICAL_PAGES)
    FreePhysicalPage(old_page);  // Unallocate the existing physical page.

  shared_memory->physical_pages[page] = physical_address;

  // Now each process needs to know about the shared memory page.
  MapSharedMemoryPageInEachProcess(shared_memory, page);
}

// Handles a page fault because the process tried to access an unallocated page
// in a shared memory block.
bool HandleSharedMessagePageFault(struct Process* process,
                                  struct SharedMemory* shared_memory,
                                  size_t page) {
  struct Process* creator = GetProcessFromPid(shared_memory->creator_pid);

  // Should we create the page?
  if (creator == NULL || process == creator) {
    // Either the creator no longer exists, or we are the creator. We'll create
    // the page.
    size_t physical_address = GetPhysicalPage();
    if (physical_address == OUT_OF_PHYSICAL_PAGES) return false;  // Out of memory.

    MapPhysicalPageInSharedMemory(shared_memory, page, physical_address);

  } else {
    // We are not the creator. We'll message the creator and sleep this thread.
    SleepThreadUntilSharedMemoryPageIsCreatedAndNotifyCreator(shared_memory,
                                                              page, creator);
  }
  return true;
}

bool MaybeHandleSharedMessagePageFault(size_t address) {
  if (running_thread == NULL) {
    // This exception occured in the kernel.
    return false;
  }

  // Round address down to the page it's in.
  address &= (PAGE_SIZE - 1);

  struct Process* process = running_thread->process;

  // Loop through each shared memory in process.
  for (struct SharedMemoryInProcess* current_shared_memory_in_process =
           process->shared_memory;
       current_shared_memory_in_process != NULL;
       current_shared_memory_in_process =
           current_shared_memory_in_process->next_in_process) {
    // Does this address fall within the shared memory block?
    if (address < current_shared_memory_in_process->virtual_address)
      continue;  // Address is too low.

    size_t page_in_shared_memory =
        (address - current_shared_memory_in_process->virtual_address) /
        PAGE_SIZE;

    struct SharedMemory* shared_memory =
        current_shared_memory_in_process->shared_memory;

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
  struct SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
  if (shared_memory == NULL) return false;

  size_t page_in_shared_memory = offset_in_shared_memory / PAGE_SIZE;
  if (page_in_shared_memory >= shared_memory->size_in_pages)
    return false;  // Address is far too high.

  // Check that the page has a physical page allocated to it.
  return shared_memory->physical_pages[page_in_shared_memory] !=
         OUT_OF_PHYSICAL_PAGES;
}

bool CanProcessWriteToSharedMemory(struct Process* process,
                                   struct SharedMemory* shared_memory) {
  // Either the shared memory is writable by everyone, or this process is the
  // creator of the shared memory.
  return ((shared_memory->flags & SM_JOINERS_CAN_WRITE) != 0) ||
         shared_memory->creator_pid == process->pid;
}
