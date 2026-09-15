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

#include "ipc/shared_memory.h"

#include "ipc/shared_memory_manager.h"
#include "processes/process.h"

namespace ipc {

using processes::Process;

containers::RecursiveInterruptSafeSpinlock& GetSharedMemoryLock() {
  return SharedMemoryManager::Get().lock();
}

void InitializeSharedMemory() {
  SharedMemoryManager::Get().Initialize();
}

SharedMemoryInProcess* CreateAndMapSharedMemoryBlockIntoProcess(
    Process* process, size_t pages, size_t flags,
    size_t message_id_for_lazily_loaded_pages) {
  return SharedMemoryManager::Get().CreateAndMap(
      process, pages, flags, message_id_for_lazily_loaded_pages);
}

void ReleaseSharedMemoryBlock(SharedMemory* shared_memory) {
  SharedMemoryManager::Get().Release(shared_memory);
}

SharedMemoryInProcess* JoinSharedMemory(Process* process,
                                        size_t shared_memory_id) {
  return SharedMemoryManager::Get().Join(process, shared_memory_id);
}

bool JoinChildProcessInSharedMemory(Process* parent, Process* child,
                                    size_t shared_memory_id,
                                    size_t starting_address) {
  return SharedMemoryManager::Get().JoinChild(parent, child, shared_memory_id,
                                              starting_address);
}

void LeaveSharedMemory(Process* process, size_t shared_memory_id) {
  SharedMemoryManager::Get().Leave(process, shared_memory_id);
}

void MovePageIntoSharedMemory(Process* process, size_t shared_memory_id,
                              size_t offset_in_buffer, size_t page_address) {
  SharedMemoryManager::Get().MovePageInto(process, shared_memory_id,
                                          offset_in_buffer, page_address);
}

bool MaybeHandleSharedMessagePageFault(size_t address) {
  return SharedMemoryManager::Get().MaybeHandlePageFault(address);
}

bool IsAddressAllocatedInSharedMemory(size_t shared_memory_id,
                                      size_t offset_in_shared_memory) {
  return SharedMemoryManager::Get().IsAddressAllocated(shared_memory_id,
                                                       offset_in_shared_memory);
}

size_t GetPhysicalAddressOfPageInSharedMemory(size_t shared_memory_id,
                                              size_t offset_in_shared_memory) {
  return SharedMemoryManager::Get().GetPhysicalAddressOfPage(
      shared_memory_id, offset_in_shared_memory);
}

void GrantPermissionToAllocateIntoSharedMemory(Process* grantor,
                                               size_t shared_memory_id,
                                               size_t grantee_pid) {
  SharedMemoryManager::Get().GrantPermissionToAllocate(grantor, shared_memory_id,
                                                       grantee_pid);
}

bool CanProcessWriteToSharedMemory(Process* process,
                                   SharedMemory* shared_memory) {
  return SharedMemoryManager::Get().CanProcessWrite(process, shared_memory);
}

void GetSharedMemoryDetailsPertainingToProcess(Process* process,
                                               size_t shared_memory_id,
                                               size_t& flags,
                                               size_t& size_in_bytes) {
  SharedMemoryManager::Get().GetDetailsPertainingToProcess(
      process, shared_memory_id, flags, size_in_bytes);
}

SharedMemoryInProcess* GrowSharedMemory(Process* process,
                                        size_t shared_memory_id, size_t pages) {
  return SharedMemoryManager::Get().Grow(process, shared_memory_id, pages);
}

void RegisterSharedMemoryEvent(Process* process, size_t shared_memory_id,
                               size_t offset, size_t message_id) {
  SharedMemoryManager::Get().RegisterEvent(process, shared_memory_id, offset,
                                           message_id);
}

void UnregisterSharedMemoryEvent(Process* process, size_t shared_memory_id,
                                 size_t offset) {
  SharedMemoryManager::Get().UnregisterEvent(process, shared_memory_id, offset);
}

void TriggerSharedMemoryEvent(size_t shared_memory_id, size_t offset) {
  SharedMemoryManager::Get().TriggerEvent(shared_memory_id, offset);
}

void UnregisterAllSharedMemoryEventsForProcess(Process* process) {
  SharedMemoryManager::Get().UnregisterAllEventsForProcess(process);
}

size_t GetAllocatedSharedMemoryInBytes() {
  return SharedMemoryManager::Get().GetAllocatedBytes();
}

}  // namespace ipc
