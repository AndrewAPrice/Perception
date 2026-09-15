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

#pragma once

#include "containers/aa_tree.h"
#include "containers/spinlock.h"
#include "ipc/shared_memory.h"
#include "types.h"

namespace processes {
struct Process;
}

namespace ipc {

// Manager overseeing shared memory allocation, mapping, and events across processes.
class SharedMemoryManager {
 public:
  // Returns the singleton instance of SharedMemoryManager.
  static SharedMemoryManager& Get();

  // Constructs the SharedMemoryManager.
  SharedMemoryManager();

  // Initializes internal structures for shared memory tracking.
  void Initialize();

  // Creates a shared memory block and maps it into a process.
  SharedMemoryInProcess* CreateAndMap(processes::Process* process,
                                      size_t pages, size_t flags,
                                      size_t message_id_for_lazily_loaded_pages);

  // Releases a shared memory block.
  void Release(SharedMemory* shared_memory);

  // Joins a shared memory block into a process.
  SharedMemoryInProcess* Join(processes::Process* process,
                             size_t shared_memory_id);

  // Makes a child process join a shared memory block at starting_address.
  bool JoinChild(processes::Process* parent, processes::Process* child,
                 size_t shared_memory_id, size_t starting_address);

  // Leaves a shared memory block for a process.
  void Leave(processes::Process* process, size_t shared_memory_id);

  // Moves a page from a process into a shared memory block.
  void MovePageInto(processes::Process* process, size_t shared_memory_id,
                    size_t offset_in_buffer, size_t page_address);

  // Handles a page fault for a lazily loaded shared memory page if applicable.
  bool MaybeHandlePageFault(size_t address);

  // Returns whether an address offset is allocated in shared memory.
  bool IsAddressAllocated(size_t shared_memory_id,
                          size_t offset_in_shared_memory);

  // Returns the physical address of a page in shared memory.
  size_t GetPhysicalAddressOfPage(size_t shared_memory_id,
                                 size_t offset_in_shared_memory);

  // Grants permission for another process to allocate into a shared memory block.
  void GrantPermissionToAllocate(processes::Process* grantor,
                                 size_t shared_memory_id,
                                 size_t grantee_pid);

  // Returns whether process has write permissions to shared memory.
  bool CanProcessWrite(processes::Process* process,
                       SharedMemory* shared_memory);

  // Queries shared memory details pertaining to a process.
  void GetDetailsPertainingToProcess(processes::Process* process,
                                     size_t shared_memory_id,
                                     size_t& flags,
                                     size_t& size_in_bytes);

  // Grows the size of a shared memory block in pages.
  SharedMemoryInProcess* Grow(processes::Process* process,
                              size_t shared_memory_id, size_t pages);

  // Registers a shared memory event for a process.
  void RegisterEvent(processes::Process* process, size_t shared_memory_id,
                     size_t offset, size_t message_id);

  // Unregisters a shared memory event for a process.
  void UnregisterEvent(processes::Process* process, size_t shared_memory_id,
                       size_t offset);

  // Triggers a shared memory event at the given offset.
  void TriggerEvent(size_t shared_memory_id, size_t offset);

  // Unregisters all shared memory events for a process.
  void UnregisterAllEventsForProcess(processes::Process* process);

  // Removes a waiting thread from any shared memory block it is waiting on.
  void RemoveWaitingThread(scheduling::Thread& thread);

  // Returns total allocated bytes across all shared memory.
  size_t GetAllocatedBytes();


  // Returns spinlock synchronizing shared memory operations.
  containers::RecursiveInterruptSafeSpinlock& lock() { return lock_; }

 private:
  // Helper to allocate a new SharedMemory structure.
  SharedMemory* CreateBlock(processes::Process* process, size_t pages,
                            size_t flags,
                            size_t message_id_for_lazily_loaded_pages);

  containers::RecursiveInterruptSafeSpinlock lock_;
  size_t last_assigned_id_;
  size_t allocated_pages_;
  containers::AATree<SharedMemory, &SharedMemory::all_shared_memories_node,
                     &SharedMemory::id>
      all_shared_memories_;
};

}  // namespace ipc
