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

#include "ipc/shared_memory_manager.h"

#include "containers/object_pool.h"
#include "hardware/io.h"
#include "ipc/messages.h"
#include "memory/heap_allocator.h"
#include "memory/memory.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_allocator.h"
#include "output/text_terminal.h"
#include "processes/process.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"

namespace ipc {

using containers::ObjectPool;
using containers::RecursiveInterruptSafeSpinlockGuard;
using hardware::ReadTimestampCounter;
using memory::kOutOfPhysicalPages;
using memory::kPageSize;
using memory::FlushVirtualPage;
using memory::FreePhysicalPage;
using memory::GetPhysicalPage;
using memory::IsPageAlignedAddress;
using memory::MapSharedMemoryIntoProcess;
using memory::MapSharedMemoryIntoProcessAtAddress;
using memory::RoundDownToPageAlignedAddress;
using memory::UnmapSharedMemoryFromProcess;
using output::print;
using output::NumberFormat;
using processes::GetProcessFromPid;
using processes::IsProcessAChildOfParent;
using processes::Process;
using processes::ProcessRef;
using scheduling::RunningThread;
using scheduling::ScheduleThread;
using scheduling::Thread;
using scheduling::UnscheduleThread;

namespace {

// Global singleton instance of SharedMemoryManager.
SharedMemoryManager g_shared_memory_manager;

// Most processes that may be granted permission to assign pages to a single
// shared memory block. Grantee PIDs are chosen by the calling process and don't
// have to exist, so this bounds how much kernel memory one block can consume.
constexpr size_t kMaxGranteesPerSharedMemoryBlock = 64;

// Maximum number of pages a shared memory block can grow to (256 MB).
constexpr size_t kMaxSharedMemoryPages = 65536;

// Maximum number of shared memory events that can be registered by a single process.
constexpr size_t kMaxSharedMemoryEventsPerProcess = 64;

// Generates a 64-bit pseudo-random ID for shared memory blocks.
size_t GenerateNextSharedMemoryId() {
  static uint64 state = 0x853c49e6748fea9bULL;
  uint64 t = ReadTimestampCounter();
  state ^= t + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2);
  size_t id = state & 0x7FFFFFFFFFFFFFFFULL;
  if (id == 0) id = 1;
  return id;
}

}  // namespace

SharedMemoryManager& SharedMemoryManager::Get() {
  return g_shared_memory_manager;
}

SharedMemoryManager::SharedMemoryManager()
    : last_assigned_id_(0), allocated_pages_(0) {}

void SharedMemoryManager::Initialize() {
  lock_.Initialize();
  allocated_pages_ = 0;
  last_assigned_id_ = 0;
  new (&all_shared_memories_)
      containers::AATree<SharedMemory, &SharedMemory::all_shared_memories_node,
                         &SharedMemory::id>();
}

SharedMemory* SharedMemoryManager::CreateBlock(
    Process* process, size_t pages, size_t flags,
    size_t message_id_for_lazily_loaded_pages) {
  // Bounded before the multiplication below, which would otherwise wrap and
  // undersize the allocation.
  if (pages == 0 || pages > memory::kMaxPagesPerSyscall) return nullptr;

  SharedMemory* shared_memory = ObjectPool<SharedMemory>::Allocate();
  if (shared_memory == nullptr) return nullptr;

  size_t id = 0;
  do {
    id = GenerateNextSharedMemoryId();
  } while (all_shared_memories_.SearchForItemEqualToValue(id) != nullptr);
  shared_memory->id = id;
  shared_memory->size_in_pages = pages;
  shared_memory->flags = flags;
  shared_memory->processes_referencing_this_block = 0;
  shared_memory->physical_pages = (size_t*)malloc(sizeof(size_t) * pages);

  if (shared_memory->physical_pages == nullptr) {
    ObjectPool<SharedMemory>::Release(shared_memory);
    return nullptr;
  }

  for (size_t page = 0; page < pages; page++)
    shared_memory->physical_pages[page] = kOutOfPhysicalPages;

  shared_memory->creator_pid = process->pid;
  shared_memory->message_id_for_lazily_loaded_pages =
      message_id_for_lazily_loaded_pages;

  if (!shared_memory->pids_allowed_to_assign_memory_pages.Insert(
          process->pid)) {
    free(shared_memory->physical_pages);
    ObjectPool<SharedMemory>::Release(shared_memory);
    return nullptr;
  }

  if ((flags & kSmLazilyAllocated) == 0) {
    for (size_t page = 0; page < pages; page++) {
      size_t physical_page = GetPhysicalPage();
      if (physical_page == kOutOfPhysicalPages) {
        for (size_t p = 0; p < page; p++) {
          FreePhysicalPage(shared_memory->physical_pages[p]);
          allocated_pages_--;
        }
        free(shared_memory->physical_pages);
        ObjectPool<SharedMemory>::Release(shared_memory);
        return nullptr;
      }
      shared_memory->physical_pages[page] = physical_page;
      allocated_pages_++;
    }
  }

  // Registered last, so that a block that failed part way through being created
  // is never reachable by ID.
  all_shared_memories_.Insert(shared_memory);

  return shared_memory;
}

static void MapSharedMemoryPageInEachProcessHelper(SharedMemory* shared_memory,
                                                  size_t page) {
  if (page >= shared_memory->size_in_pages) return;

  size_t physical_address = shared_memory->physical_pages[page];
  if (physical_address == kOutOfPhysicalPages) return;

  size_t offset_of_page_in_bytes = page * kPageSize;

  for (auto* shared_memory_in_process : shared_memory->joined_processes) {
    if (page >= shared_memory_in_process->mapped_pages) continue;

    Process* process = shared_memory_in_process->process;
    bool can_write =
        SharedMemoryManager::Get().CanProcessWrite(process, shared_memory);
    size_t virtual_address =
        shared_memory_in_process->virtual_address + offset_of_page_in_bytes;
    process->virtual_address_space.MapPhysicalPageAt(
        virtual_address, physical_address, false, can_write, false);
  }

  for (ThreadWaitingForSharedMemoryPage* waiting_thread =
           shared_memory->waiting_threads.FirstItem();
       waiting_thread != nullptr;) {
    auto* next = shared_memory->waiting_threads.NextItem(waiting_thread);
    if (waiting_thread->page == page) {
      shared_memory->waiting_threads.Remove(waiting_thread);
      ScheduleThread(waiting_thread->thread);
      ObjectPool<ThreadWaitingForSharedMemoryPage>::Release(waiting_thread);
    }
    waiting_thread = next;
  }
}

static void MapPhysicalPageInSharedMemoryHelper(SharedMemory* shared_memory,
                                               size_t page,
                                               size_t physical_address,
                                               size_t& allocated_pages) {
  size_t old_page = shared_memory->physical_pages[page];
  if (old_page == physical_address) return;

  if (old_page != kOutOfPhysicalPages) {
    FreePhysicalPage(old_page);
    size_t offset_of_page_in_bytes = page * kPageSize;
    for (auto* shared_memory_in_process : shared_memory->joined_processes) {
      Process* process = shared_memory_in_process->process;
      size_t virtual_address =
          shared_memory_in_process->virtual_address + offset_of_page_in_bytes;
      process->virtual_address_space.ReleasePages(virtual_address, 1);
    }
  } else {
    allocated_pages++;
  }

  shared_memory->physical_pages[page] = physical_address;
  MapSharedMemoryPageInEachProcessHelper(shared_memory, page);
}

static bool SleepThreadUntilSharedMemoryPageIsCreatedHelper(
    SharedMemory* shared_memory, size_t page, Process* creator) {
  if (page >= shared_memory->size_in_pages) return false;
  if (shared_memory->physical_pages[page] != kOutOfPhysicalPages) return true;

  Thread* thread = RunningThread();
  auto waiting_thread =
      ObjectPool<ThreadWaitingForSharedMemoryPage>::Allocate();
  if (waiting_thread == nullptr) return false;

  waiting_thread->thread = thread;
  waiting_thread->shared_memory = shared_memory;
  waiting_thread->page = page;

  shared_memory->waiting_threads.AddBack(waiting_thread);
  thread->thread_is_waiting_for_shared_memory = waiting_thread;
  UnscheduleThread(thread, scheduling::ThreadState::BlockedOnMemory);

  SendKernelMessageToProcess(creator,
                             shared_memory->message_id_for_lazily_loaded_pages,
                             page * kPageSize, 0, 0, 0, 0);
  return true;
}

static bool HandleSharedMessagePageFaultHelper(Process* process,
                                              SharedMemory* shared_memory,
                                              size_t page,
                                              size_t& allocated_pages) {
  ProcessRef creator = GetProcessFromPid(shared_memory->creator_pid);

  if (!creator || process == creator.get() ||
      shared_memory->pids_allowed_to_assign_memory_pages.Contains(
          process->pid)) {
    size_t physical_address = GetPhysicalPage();
    if (physical_address == kOutOfPhysicalPages) return false;
    MapPhysicalPageInSharedMemoryHelper(shared_memory, page, physical_address,
                                        allocated_pages);
  } else {
    return SleepThreadUntilSharedMemoryPageIsCreatedHelper(
        shared_memory, page, creator.get());
  }

  return true;
}

SharedMemoryInProcess* SharedMemoryManager::CreateAndMap(
    Process* process, size_t pages, size_t flags,
    size_t message_id_for_lazily_loaded_pages) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  SharedMemory* shared_memory = CreateBlock(
      process, pages, flags, message_id_for_lazily_loaded_pages);
  if (shared_memory == nullptr) return nullptr;

  SharedMemoryInProcess* shared_memory_in_process =
      MapSharedMemoryIntoProcess(process, shared_memory);
  if (shared_memory_in_process == nullptr)
    ObjectPool<SharedMemory>::Release(shared_memory);
  return shared_memory_in_process;
}

void SharedMemoryManager::Release(SharedMemory* shared_memory) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (shared_memory->processes_referencing_this_block > 0) {
    print << "Attempting to release shared memory that still is being "
             "referenced by a process.\n";
    return;
  }
  if (!shared_memory->waiting_threads.IsEmpty()) {
    print << "Attempting to release shared memory that still is blocking "
             "other threads.\n";
    return;
  }

  for (size_t page = 0; page < shared_memory->size_in_pages; page++) {
    if (shared_memory->physical_pages[page] != kOutOfPhysicalPages) {
      FreePhysicalPage(shared_memory->physical_pages[page]);
      allocated_pages_--;
    }
  }
  free(shared_memory->physical_pages);

  while (auto* event = shared_memory->events.FirstItem()) {
    shared_memory->events.Remove(event);
    event->process->shared_memory_events.Remove(event);
    ObjectPool<SharedMemoryEvent>::Release(event);
  }

  all_shared_memories_.Remove(shared_memory);
  ObjectPool<SharedMemory>::Release(shared_memory);
}

static SharedMemoryInProcess* FindSharedMemoryInProcessHelper(
    Process* process, size_t shared_memory_id) {
  for (auto* shared_memory_in_process : process->joined_shared_memories) {
    if (shared_memory_in_process->shared_memory->id == shared_memory_id)
      return shared_memory_in_process;
  }
  return nullptr;
}

static SharedMemoryInProcess* RejoinSharedMemoryHelper(
    SharedMemoryInProcess* shared_memory_in_process) {
  auto shared_memory = shared_memory_in_process->shared_memory;
  auto process = shared_memory_in_process->process;

  shared_memory->processes_referencing_this_block++;
  UnmapSharedMemoryFromProcess(shared_memory_in_process);
  shared_memory_in_process = MapSharedMemoryIntoProcess(process, shared_memory);
  shared_memory->processes_referencing_this_block--;
  return shared_memory_in_process;
}

SharedMemoryInProcess* SharedMemoryManager::Join(Process* process,
                                                size_t shared_memory_id) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  auto* shared_memory_in_process =
      FindSharedMemoryInProcessHelper(process, shared_memory_id);
  if (shared_memory_in_process != nullptr) {
    SharedMemory* shared_memory = shared_memory_in_process->shared_memory;
    if (shared_memory_in_process->mapped_pages != shared_memory->size_in_pages)
      return RejoinSharedMemoryHelper(shared_memory_in_process);
    return shared_memory_in_process;
  }

  SharedMemory* shared_memory =
      all_shared_memories_.SearchForItemEqualToValue(shared_memory_id);
  if (shared_memory == nullptr) return nullptr;

  return MapSharedMemoryIntoProcess(process, shared_memory);
}

bool SharedMemoryManager::JoinChild(Process* parent, Process* child,
                                    size_t shared_memory_id,
                                    size_t starting_address) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (!IsProcessAChildOfParent(parent, child)) return false;

  SharedMemory* shared_memory =
      all_shared_memories_.SearchForItemEqualToValue(shared_memory_id);
  if (shared_memory == nullptr) return false;

  if (!IsPageAlignedAddress(starting_address)) {
    print << "JoinChildProcessInSharedMemory called with non page aligned "
             "address: "
          << NumberFormat::Hexidecimal << starting_address << '\n';
    starting_address = RoundDownToPageAlignedAddress(starting_address);
  }

  if (!child->virtual_address_space.ReserveAddressRange(
          starting_address, shared_memory->size_in_pages))
    return false;

  return MapSharedMemoryIntoProcessAtAddress(child, shared_memory,
                                             starting_address) != nullptr;
}

void SharedMemoryManager::Leave(Process* process, size_t shared_memory_id) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  auto* shared_memory_in_process =
      FindSharedMemoryInProcessHelper(process, shared_memory_id);
  if (shared_memory_in_process == nullptr) return;
  UnmapSharedMemoryFromProcess(shared_memory_in_process);
}

void SharedMemoryManager::MovePageInto(Process* process,
                                       size_t shared_memory_id,
                                       size_t offset_in_buffer,
                                       size_t page_address) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (process == nullptr) return;

  size_t physical_address =
      process->virtual_address_space.GetPhysicalAddress(page_address, true);
  if (physical_address == kOutOfMemory) return;

  process->virtual_address_space.ReleasePages(page_address, 1);

  SharedMemory* shared_memory =
      all_shared_memories_.SearchForItemEqualToValue(shared_memory_id);
  if (shared_memory == nullptr) {
    FreePhysicalPage(physical_address);
    return;
  }

  if (!shared_memory->pids_allowed_to_assign_memory_pages.Contains(
          process->pid)) {
    FreePhysicalPage(physical_address);
    return;
  }

  size_t page = offset_in_buffer / kPageSize;
  if (page >= shared_memory->size_in_pages) {
    FreePhysicalPage(physical_address);
    return;
  }

#ifndef TEST
  __asm__ __volatile__("mfence" ::: "memory");
#endif
  MapPhysicalPageInSharedMemoryHelper(shared_memory, page, physical_address,
                                      allocated_pages_);
}

bool SharedMemoryManager::MaybeHandlePageFault(size_t address) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (RunningThread() == nullptr) return false;

  address &= ~(kPageSize - 1);
  Process* process = RunningThread()->process;

  SharedMemoryInProcess* shared_memory_in_process =
      process->joined_shared_memories.SearchForItemLessThanOrEqualToValue(
          address);
  if (shared_memory_in_process == nullptr) return false;

  SharedMemory* shared_memory = shared_memory_in_process->shared_memory;
  size_t page_in_shared_memory =
      (address - shared_memory_in_process->virtual_address) / kPageSize;

  if (page_in_shared_memory >= shared_memory_in_process->mapped_pages)
    return false;

  if (page_in_shared_memory < shared_memory->size_in_pages) {
    if ((shared_memory->flags & kSmLazilyAllocated) == 0) return false;

    if (shared_memory->physical_pages[page_in_shared_memory] ==
        kOutOfPhysicalPages) {
      return HandleSharedMessagePageFaultHelper(
          process, shared_memory, page_in_shared_memory, allocated_pages_);
    } else {
      bool can_write = CanProcessWrite(process, shared_memory);
      process->virtual_address_space.MapPhysicalPageAt(
          address, shared_memory->physical_pages[page_in_shared_memory], false,
          can_write, false);
      FlushVirtualPage(address);
      return true;
    }
  }
  return false;
}

bool SharedMemoryManager::IsAddressAllocated(size_t shared_memory_id,
                                            size_t offset_in_shared_memory) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  return GetPhysicalAddressOfPage(shared_memory_id, offset_in_shared_memory) !=
         kOutOfPhysicalPages;
}

size_t SharedMemoryManager::GetPhysicalAddressOfPage(
    size_t shared_memory_id, size_t offset_in_shared_memory) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  SharedMemory* shared_memory =
      all_shared_memories_.SearchForItemEqualToValue(shared_memory_id);
  if (shared_memory == nullptr) return kOutOfPhysicalPages;

  size_t page_in_shared_memory = offset_in_shared_memory / kPageSize;
  if (page_in_shared_memory >= shared_memory->size_in_pages)
    return kOutOfPhysicalPages;

  return shared_memory->physical_pages[page_in_shared_memory];
}

void SharedMemoryManager::GrantPermissionToAllocate(Process* grantor,
                                                    size_t shared_memory_id,
                                                    size_t grantee_pid) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  SharedMemory* shared_memory =
      all_shared_memories_.SearchForItemEqualToValue(shared_memory_id);
  if (shared_memory == nullptr) return;
  auto& grantees = shared_memory->pids_allowed_to_assign_memory_pages;
  if (!grantees.Contains(grantor->pid)) return;
  if (grantees.Size() >= kMaxGranteesPerSharedMemoryBlock) return;

  grantees.Insert(grantee_pid);
}

bool SharedMemoryManager::CanProcessWrite(Process* process,
                                         SharedMemory* shared_memory) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  return ((shared_memory->flags & kSmJoinersCanWrite) != 0) ||
         shared_memory->creator_pid == process->pid;
}

void SharedMemoryManager::GetDetailsPertainingToProcess(
    Process* process, size_t shared_memory_id, size_t& flags,
    size_t& size_in_bytes) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  SharedMemory* shared_memory =
      all_shared_memories_.SearchForItemEqualToValue(shared_memory_id);
  if (shared_memory == nullptr) {
    flags = 0;
    size_in_bytes = 0;
    return;
  }

  flags = kSmdExists;
  if (((shared_memory->flags & kSmJoinersCanWrite) != 0) ||
      shared_memory->creator_pid == process->pid) {
    flags |= kSmdCanProcessWrite;
  }

  if ((shared_memory->flags & kSmLazilyAllocated) != 0)
    flags |= kSmdLazilyAllocated;

  if (shared_memory->creator_pid == process->pid ||
      shared_memory->pids_allowed_to_assign_memory_pages.Contains(process->pid)) {
    flags |= kSmdCanProcessAssignPages;
  }
  size_in_bytes = shared_memory->size_in_pages * kPageSize;
}

SharedMemoryInProcess* SharedMemoryManager::Grow(Process* process,
                                                size_t shared_memory_id,
                                                size_t pages) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  auto* shared_memory_in_process =
      FindSharedMemoryInProcessHelper(process, shared_memory_id);
  if (shared_memory_in_process == nullptr) return nullptr;

  auto* shared_memory = shared_memory_in_process->shared_memory;
  size_t current_size_in_pages = shared_memory->size_in_pages;

  if (pages <= current_size_in_pages) {
    if (current_size_in_pages != shared_memory_in_process->mapped_pages)
      return RejoinSharedMemoryHelper(shared_memory_in_process);
    return shared_memory_in_process;
  }

  if (shared_memory->creator_pid != process->pid &&
      !shared_memory->pids_allowed_to_assign_memory_pages.Contains(process->pid))
    return shared_memory_in_process;

  // Bounded before the multiplication below, which would otherwise wrap and
  // undersize the allocation.
  if (pages > kMaxSharedMemoryPages) return shared_memory_in_process;

  size_t* newer_physical_pages = (size_t*)malloc(sizeof(size_t) * pages);
  if (newer_physical_pages == nullptr) return shared_memory_in_process;

  for (size_t page = 0; page < current_size_in_pages; page++)
    newer_physical_pages[page] = shared_memory->physical_pages[page];

  if ((shared_memory->flags & kSmLazilyAllocated) == 0) {
    for (size_t page = current_size_in_pages; page < pages; page++) {
      size_t physical_page = GetPhysicalPage();
      if (physical_page == kOutOfPhysicalPages) {
        for (size_t p = current_size_in_pages; p < page; p++) {
          FreePhysicalPage(newer_physical_pages[p]);
          allocated_pages_--;
        }
        free(newer_physical_pages);
        return shared_memory_in_process;
      }
      newer_physical_pages[page] = physical_page;
      allocated_pages_++;
    }
  } else {
    for (size_t page = current_size_in_pages; page < pages; page++)
      newer_physical_pages[page] = kOutOfPhysicalPages;
  }

  free(shared_memory->physical_pages);
  shared_memory->physical_pages = newer_physical_pages;
  shared_memory->size_in_pages = pages;

  return RejoinSharedMemoryHelper(shared_memory_in_process);
}

void SharedMemoryManager::RegisterEvent(Process* process,
                                       size_t shared_memory_id, size_t offset,
                                       size_t message_id) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  SharedMemory* shared_memory =
      all_shared_memories_.SearchForItemEqualToValue(shared_memory_id);
  if (shared_memory == nullptr) return;
  if (offset >= shared_memory->size_in_pages * kPageSize) return;

  size_t count = 0;
  for (auto* event : process->shared_memory_events) {
    if (event->shared_memory == shared_memory && event->offset == offset) {
      event->message_id = message_id;
      return;
    }
    count++;
  }
  if (count >= kMaxSharedMemoryEventsPerProcess) return;

  SharedMemoryEvent* event = ObjectPool<SharedMemoryEvent>::Allocate();
  if (event == nullptr) return;

  event->process = process;
  event->shared_memory = shared_memory;
  event->offset = offset;
  event->message_id = message_id;

  shared_memory->events.AddBack(event);
  process->shared_memory_events.AddBack(event);
}

void SharedMemoryManager::UnregisterEvent(Process* process,
                                         size_t shared_memory_id,
                                         size_t offset) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  SharedMemory* shared_memory =
      all_shared_memories_.SearchForItemEqualToValue(shared_memory_id);
  if (shared_memory == nullptr) return;

  for (auto* event : shared_memory->events) {
    if (event->process == process && event->offset == offset) {
      shared_memory->events.Remove(event);
      process->shared_memory_events.Remove(event);
      ObjectPool<SharedMemoryEvent>::Release(event);
      return;
    }
  }
}

void SharedMemoryManager::TriggerEvent(size_t shared_memory_id, size_t offset) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (RunningThread() != nullptr &&
      FindSharedMemoryInProcessHelper(RunningThread()->process,
                                      shared_memory_id) == nullptr) {
    return;
  }
  SharedMemory* shared_memory =
      all_shared_memories_.SearchForItemEqualToValue(shared_memory_id);
  if (shared_memory == nullptr) return;

  for (auto* event = shared_memory->events.FirstItem(); event != nullptr;) {
    auto* next = shared_memory->events.NextItem(event);
    if (event->offset == offset) {
      SendKernelMessageToProcess(event->process, event->message_id, 0, 0, 0, 0,
                                 0);
      shared_memory->events.Remove(event);
      event->process->shared_memory_events.Remove(event);
      ObjectPool<SharedMemoryEvent>::Release(event);
    }
    event = next;
  }
}

void SharedMemoryManager::UnregisterAllEventsForProcess(Process* process) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  while (auto* event = process->shared_memory_events.FirstItem()) {
    process->shared_memory_events.Remove(event);
    event->shared_memory->events.Remove(event);
    ObjectPool<SharedMemoryEvent>::Release(event);
  }
}

void SharedMemoryManager::RemoveWaitingThread(Thread& thread) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  auto* wait_record = thread.thread_is_waiting_for_shared_memory;
  if (wait_record != nullptr) {
    wait_record->shared_memory->waiting_threads.Remove(wait_record);
    thread.thread_is_waiting_for_shared_memory = nullptr;
    ObjectPool<ThreadWaitingForSharedMemoryPage>::Release(wait_record);
  }
}

size_t SharedMemoryManager::GetAllocatedBytes() {

  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  return allocated_pages_ * kPageSize;
}

}  // namespace ipc
