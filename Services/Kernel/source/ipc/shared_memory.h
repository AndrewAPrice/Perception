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

#include "containers/aa_tree.h"
#include "containers/linked_list.h"
#include "containers/set.h"
#include "ipc/shared_memory_event.h"
#include "containers/spinlock.h"
#include "types.h"

namespace processes {
struct Process;
}

namespace scheduling {
struct Thread;
}

namespace ipc {

struct SharedMemory;
struct SharedMemoryInProcess;

// Returns the spinlock protecting all shared memory operations.
containers::RecursiveInterruptSafeSpinlock& GetSharedMemoryLock();

// Shared memory bitwise flags:
// Is this shared memory lazily allocated?
constexpr size_t kSmLazilyAllocated = (1 << 0);

// Can joiners (not the creator of the shared memory) write to it?
constexpr size_t kSmJoinersCanWrite = (1 << 1);

// Shared memory details bitwise flags:
// Does the shared memory exist?
constexpr size_t kSmdExists = (1 << 0);

// Can this process write to this shared memory?
constexpr size_t kSmdCanProcessWrite = (1 << 1);

// Is this shared memory lazily allocated?
constexpr size_t kSmdLazilyAllocated = (1 << 2);

// Can this process assign pages to this shared memory?
constexpr size_t kSmdCanProcessAssignPages = (1 << 3);

// Represents a thread that is waiting for a shared memory page.
struct ThreadWaitingForSharedMemoryPage {
  // The thread that is waiting.
  scheduling::Thread* thread;

  // The shared memory the page the thread is waiting for is a part of.
  SharedMemory* shared_memory;

  // The index of the page the thread is waiting for.
  size_t page;

  // Linked list structure of threads waiting in the shared memory.
  containers::LinkedListNode node;
};

// Represents a block of shared memory mapped into a process.
struct SharedMemoryInProcess {
  // The referenced shared memory block.
  SharedMemory* shared_memory;

  // The containing process.
  processes::Process* process;

  // The virtual address of this shared memory block.
  size_t virtual_address;

  // The number of pages mapped into the process from the shared memory block.
  // This may be smaller than the shared memory's size as it may have grown
  // after this block was mapped into a process.
  size_t mapped_pages;

  // AA tree node of shared memory block in the process.
  containers::AATreeNode node_in_process;

  // Linked list in SharedMemory.
  containers::LinkedListNode node_in_shared_memory;
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
  // A value of kOutOfPhysicalPages means this particular page
  // doesn't have any memory allocated to it.
  size_t* physical_pages;

  // Number of processes that are referencing this block.
  size_t processes_referencing_this_block;

  // The process that created this shared memory.
  size_t creator_pid;

  // Processes that are allowed to assign memory pages.
  containers::Set<size_t> pids_allowed_to_assign_memory_pages;

  // Message ID to send to the creator if another process accesses a lazily
  // loaded memory page that hasn't been loaded yet.
  size_t message_id_for_lazily_loaded_pages;

  // Node for the AA-tree of all shared memories.
  containers::AATreeNode all_shared_memories_node;

  // Linked list of threads waiting for pages to become available in this shared
  // memory.
  containers::LinkedList<ThreadWaitingForSharedMemoryPage,
                         &ThreadWaitingForSharedMemoryPage::node>
      waiting_threads;

  // Linked list of processes that have joined this shared memory.
  containers::LinkedList<SharedMemoryInProcess,
                         &SharedMemoryInProcess::node_in_shared_memory>
      joined_processes;

  // Linked list of events registered for this shared memory.
  containers::LinkedList<SharedMemoryEvent,
                         &SharedMemoryEvent::node_in_shared_memory>
      events;
};

// Initializes the internal structures for shared memory.
void InitializeSharedMemory();

// Creates a shared memory block and maps it into a process.
SharedMemoryInProcess* CreateAndMapSharedMemoryBlockIntoProcess(
    processes::Process* process, size_t pages, size_t flags,
    size_t message_id_for_lazily_loaded_pages);

// Releases a shared memory block. Please make sure that there are no
// processes referencing the shared memory block before calling this.
void ReleaseSharedMemoryBlock(SharedMemory* shared_memory);

// Joins a shared memory block. Ensures that a shared memory is only mapped once
// per process.
SharedMemoryInProcess* JoinSharedMemory(processes::Process* process,
                                        size_t shared_memory_id);

// Makes a child process join a shared memory block at the given address. The
// receiving process must be created by the calling process and in the
// `creating` state. If any of the pages are already occupied in the child
// process, nothing is set.
bool JoinChildProcessInSharedMemory(processes::Process* parent,
                                    processes::Process* child,
                                    size_t shared_memory_id,
                                    size_t starting_address);

// Leaves a shared memory block, but doesn't unmap it if there are still other
// references to the shared memory block in the process.
void LeaveSharedMemory(processes::Process* process, size_t shared_memory_id);

// Moves a page into a shared memory block. Only the creator of the shared
// memory block can call this.
void MovePageIntoSharedMemory(processes::Process* process,
                              size_t shared_memory_id, size_t offset_in_buffer,
                              size_t page_address);

// Tries to handle a page fault if it's related to a lazily loaded shared
// message. Returns whether the exception was handled.
bool MaybeHandleSharedMessagePageFault(size_t address);

// Does the address exist in the shared memory block and is it allocated? Sets
// the physical address of the memory page, if it exists.
bool IsAddressAllocatedInSharedMemory(size_t shared_memory_id,
                                      size_t offset_in_shared_memory);

// Returns the physical address of a page in shared memory. Returns
// kOutOfPhysicalPages if it does not exist.
size_t GetPhysicalAddressOfPageInSharedMemory(size_t shared_memory_id,
                                              size_t offset_in_shared_memory);

// Grants permission for another process to allocate into this shared memory
// block.
void GrantPermissionToAllocateIntoSharedMemory(processes::Process* grantor,
                                               size_t shared_memory_id,
                                               size_t grantee_pid);

// Can this process write to this shared memory?
bool CanProcessWriteToSharedMemory(processes::Process* process,
                                   SharedMemory* shared_memory);

// Gets information about a shared memory buffer as it pertains to a process.
void GetSharedMemoryDetailsPertainingToProcess(processes::Process* process,
                                               size_t shared_memory_id,
                                               size_t& flags,
                                               size_t& size_in_bytes);

// Attempts to grow the size of a shared memory block.
SharedMemoryInProcess* GrowSharedMemory(processes::Process* process,
                                        size_t shared_memory_id, size_t pages);

// Registers a shared memory event.
void RegisterSharedMemoryEvent(processes::Process* process,
                               size_t shared_memory_id, size_t offset,
                               size_t message_id);

// Unregisters a shared memory event.
void UnregisterSharedMemoryEvent(processes::Process* process,
                                 size_t shared_memory_id, size_t offset);

// Triggers a shared memory event.
void TriggerSharedMemoryEvent(size_t shared_memory_id, size_t offset);

// Unregisters all shared memory events registered by the given process.
void UnregisterAllSharedMemoryEventsForProcess(processes::Process* process);

// Returns the total number of bytes currently allocated to shared memory
// blocks.
size_t GetAllocatedSharedMemoryInBytes();

}  // namespace ipc

