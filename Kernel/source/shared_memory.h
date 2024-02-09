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

#pragma once

#include "types.h"

struct Process;
struct Thread;
struct SharedMemory;
struct SharedMemoryInProcess;

// Shared memory bitwise flags:
// Is this shared memory lazily allocated?
#define SM_LAZILY_ALLOCATED (1 << 0)

// Can joiners (not the creator of the shared memory) write to it?
#define SM_JOINERS_CAN_WRITE (1 << 1)

// Represents a thread that is waiting for a shared memory page.
struct ThreadWaitingForSharedMemoryPage {
  // The thread that is waiting.
  Thread* thread;

  // The shared memory the page the thread is waiting for is a part of.
  SharedMemory* shared_memory;

  // The index of the page the thread is waiting for.
  size_t page;

  // Linked list structure of threads waiting in the shared memory.
  ThreadWaitingForSharedMemoryPage* previous;
  ThreadWaitingForSharedMemoryPage* next;
};

// Represents a block of shared memory.
struct SharedMemory {
  // The ID of this shared memory.
  size_t id;

  // The size of this shared memory block, in pages.
  size_t size_in_pages;

  // The flags the shared memory was created with.
  size_t flags;

  // Array of physical pages.
  // A value of OUT_OF_PHYSICAL_PAGES means this particular page
  // doesn't have any memory allocated to it.
  size_t* physical_pages;

  // Number of processes that are referencing this block.
  size_t processes_referencing_this_block;

  // The process that created this shared memory.
  size_t creator_pid;

  // Message ID to send to the creator if another process accesses a lazily
  // loaded memory page that hasn't been loaded yet.
  size_t message_id_for_lazily_loaded_pages;

  // Linked list of shared memory.
  SharedMemory* previous;
  SharedMemory* next;

  // Linked list of threads waiting for pages to become available in this shared
  // memory.
  ThreadWaitingForSharedMemoryPage* first_waiting_thread;

  // Linked list of processes that have joined this shared memory.
  SharedMemoryInProcess* first_process;
};

// Represents a block of shared memory mapped into a proces.
struct SharedMemoryInProcess {
  // The shared memory block we're talking about.
  SharedMemory* shared_memory;

  // The process we're in.
  Process* process;

  // The virtual address of this shared memory block.
  size_t virtual_address;

  // The next shared memory block in the process.
  SharedMemoryInProcess* next_in_process;

  // Linked list in SharedMemory.
  SharedMemoryInProcess* previous_in_shared_memory;
  SharedMemoryInProcess* next_in_shared_memory;

  // The number of references to this shared memory block in this process.
  size_t references;
};

// Initializes the internal structures for shared memory.
extern void InitializeSharedMemory();

// Creates a shared memory block and map it into a procses.
extern SharedMemoryInProcess* CreateAndMapSharedMemoryBlockIntoProcess(
    Process* process, size_t pages, size_t flags,
    size_t message_id_for_lazily_loaded_pages);

// Releases a shared memory block. Please make sure that there are no
// processes referencing the shared memory block before calling this.
extern void ReleaseSharedMemoryBlock(SharedMemory* shared_memory);

// Joins a shared memory block. Ensures that a shared memory is only mapped once
// per process.
extern SharedMemoryInProcess* JoinSharedMemory(Process* process,
                                               size_t shared_memory_id);

// Leaves a shared memory block, but doesn't unmap it if there are still other
// referenes to the shared memory block in the process.
extern void LeaveSharedMemory(Process* process, size_t shared_memory_id);

// Moves a page into a shared memory block. Only the creator of the shared
// memory block can acall this.
extern void MovePageIntoSharedMemory(Process* process, size_t shared_memory_id,
                                     size_t offset_in_buffer,
                                     size_t page_address);

// Tries to handle a page fault if it's related to a lazily loaded shared
// message. Returns if we were able to handle the exception.
extern bool MaybeHandleSharedMessagePageFault(size_t address);

// Does the address exist in the shared memory block and is it allocated?
extern bool IsAddressAllocatedInSharedMemory(size_t shared_memory_id,
                                             size_t offset_in_shared_memory);

// Can this process write to this shared memory?
extern bool CanProcessWriteToSharedMemory(Process* process,
                                          SharedMemory* shared_memory);
