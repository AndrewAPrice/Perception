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

#include "processes/process_manager.h"

#include "../../../Libraries/perception/public/perception/tracing.h"
#include "../../../Libraries/perception/public/status.h"
#include "common/kernel_string.h"
#include "containers/linked_list.h"
#include "containers/object_pool.h"
#include "diagnostics/profiling.h"
#include "hardware/io.h"
#include "interrupts/interrupts.h"
#include "ipc/messages.h"
#include "ipc/rpc.h"
#include "ipc/service.h"
#include "ipc/shared_memory.h"
#include "memory/heap_allocator.h"
#include "memory/memory.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#include "output/text_terminal.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"
#include "scheduling/timer.h"

namespace processes {

using containers::InterruptSafeSpinlockGuard;
using containers::ObjectPool;
using containers::ScopedTwoLocks;
using ::kOutOfMemory;
using memory::kPageSize;
using memory::FreePhysicalPage;
using memory::IsPageAlignedAddress;
using memory::RoundDownToPageAlignedAddress;
using output::print;
using output::NumberFormat;
using scheduling::GetCurrentTimestampInMicroseconds;
using scheduling::kMaxCores;
using scheduling::Thread;

namespace {

// Releases a ProcessToNotifyOnExit object and disconnects it from the linked lists.
void ReleaseNotification(ProcessToNotifyOnExit* notification) {
  {
    ScopedTwoLocks locks(&notification->target->lock,
                         &notification->notifyee->lock);
    notification->target->processes_to_notify_when_i_die.Remove(notification);
    notification->notifyee->processes_i_want_to_be_notified_of_when_they_die
        .Remove(notification);
  }
  ObjectPool<ProcessToNotifyOnExit>::Release(notification);
}

// Removes a child process of a parent, and returns true if the process was a
// non-nullptr child of the parent before removal.
bool RemoveChildProcessOfParent(Process* parent, Process* child) {
  if (child == nullptr || parent == nullptr) return false;
  InterruptSafeSpinlockGuard guard(parent->lock);
  if (parent->child_processes == nullptr) return false;
  if (child->parent != parent) return false;

  if (child == parent->child_processes) {
    parent->child_processes = child->next_child_process_in_parent;
    child->parent = nullptr;
    return true;
  }

  Process* previous_child = parent->child_processes;
  Process* child_in_parent = previous_child->next_child_process_in_parent;

  while (child_in_parent != nullptr) {
    if (child_in_parent == child) {
      previous_child->next_child_process_in_parent =
          child_in_parent->next_child_process_in_parent;
      child->parent = nullptr;
      return true;
    }
    previous_child = child_in_parent;
    child_in_parent = child_in_parent->next_child_process_in_parent;
  }

  return false;
}

// Checks if two process names match.
bool DoProcessNamesMatch(const char* a, const char* b) {
  return common::WordsEqual(a, b, kProcessNameWords);
}

// Global singleton instance of ProcessManager.
ProcessManager g_process_manager;

}  // namespace

ProcessManager::ProcessManager() : last_assigned_pid_(0) {
  for (size_t i = 0; i < kMaxFastProcesses; i++) processes_by_pid_[i] = nullptr;
}

ProcessManager& ProcessManager::Get() {
  return g_process_manager;
}

void ProcessManager::Initialize() {
  InterruptSafeSpinlockGuard guard(lock_);
  last_assigned_pid_ = 0;
  for (size_t i = 0; i < kMaxFastProcesses; i++) processes_by_pid_[i] = nullptr;
  new (&all_processes_)
      containers::AATree<Process, &Process::node_in_all_processes,
                         &Process::pid>();
}

Process* ProcessManager::CreateProcess(bool is_driver,
                                      bool can_create_processes,
                                      bool can_set_focus,
                                      bool can_terminate_processes,
                                      const char* name) {
  Process* proc = (Process*)malloc(sizeof(Process));
  if (proc == nullptr) return nullptr;

  new (proc) Process();
  proc->is_driver = is_driver;
  proc->can_create_processes = can_create_processes;
  proc->can_set_focus = can_set_focus;
  proc->can_terminate_processes = can_terminate_processes;
  proc->parent_pid = 0;
  memset((char*)proc->name, 0, kProcessNameLength + 1);
  if (name != nullptr) {
    common::CopyString(name, kProcessNameLength, kProcessNameLength,
                       proc->name);
  }


  if (!proc->virtual_address_space.InitializeUserSpace()) {
    proc->~Process();
    free(proc);
    return nullptr;
  }

  proc->parent = nullptr;
  proc->child_processes = nullptr;
  proc->next_child_process_in_parent = nullptr;
  proc->service_count = 0;
  proc->rpc_count = 0;
  proc->next_synthetic_rpc_response_message_id = 0;
  proc->futex_wake_message_id = 0;
  proc->timer_event_count = 0;
  proc->time_info_subscription_count = 0;

  proc->has_enabled_profiling = 0;
  proc->cycles_spent_executing_while_profiled = 0;

  proc->creation_timestamp = GetCurrentTimestampInMicroseconds();
  proc->last_updated_epoch = 0;
  proc->is_on_active_list_this_epoch = false;
  proc->tracking_cpu_usage = false;
  for (int c = 0; c < kMaxCores; c++) {
    proc->cpu_time_in_current_epoch[c] = 0;
    proc->rolling_cpu_percentage[c] = 0;
  }

  {
    InterruptSafeSpinlockGuard guard(lock_);
    last_assigned_pid_++;
    proc->pid = last_assigned_pid_;
    if (proc->pid < kMaxFastProcesses)
      __atomic_store_n(&processes_by_pid_[proc->pid], proc, __ATOMIC_RELEASE);
    all_processes_.Insert(proc);
  }
  return proc;
}

void ProcessManager::DestroyProcess(Process* process) {
  // Whichever core wins this race performs the teardown. Every other caller
  // returns immediately, which makes destroying a process idempotent even when
  // the last thread exits on one core while a parent kills it on another.
  bool expected = false;
  if (!__atomic_compare_exchange_n(&process->is_dying, &expected, true,
                                   /*weak=*/false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    return;

  {
    InterruptSafeSpinlockGuard guard(lock_);
    if (process->pid < kMaxFastProcesses)
      __atomic_store_n(&processes_by_pid_[process->pid], nullptr,
                       __ATOMIC_RELEASE);
    all_processes_.Remove(process);
  }

  while (true) {
    Process* child = nullptr;
    {
      InterruptSafeSpinlockGuard guard(process->lock);
      child = process->child_processes;
    }
    if (child == nullptr) break;
    DestroyProcess(child);
    // DestroyProcess unlinks the child from this process, but a child that is
    // already being torn down by another core returns immediately without
    // having done so yet. Unlink it here so this loop cannot spin forever.
    RemoveChildProcessOfParent(process, child);
  }

  diagnostics::NotifyProfilerThatProcessExited(process);

  if (process->parent) {
    RemoveChildProcessOfParent(process->parent, process);
  }

  scheduling::DestroyThreadsForProcess(process, true);
  scheduling::RemoveProcessFromCpuTracking(process);

  if (scheduling::GetFocusedProcess() == process)
    scheduling::SetFocusedProcess(nullptr);

  // Unregistered before the message and RPC lists are torn down, so an IRQ
  // taken on another core cannot enqueue into a process that is mid-teardown.
  interrupts::UnregisterAllMessagesToFireOnInterruptForProcess(process);

  while (!process->services_i_want_to_be_notified_of_when_they_appear.IsEmpty())
    ipc::StopNotifyingProcessWhenServiceAppears(
        process->services_i_want_to_be_notified_of_when_they_appear
            .FirstItem());

  while (
      !process->services_i_want_to_be_notified_of_when_they_disappear.IsEmpty())
    ipc::StopNotifyingProcessWhenServiceDisappears(
        process->services_i_want_to_be_notified_of_when_they_disappear
            .FirstItem());

  while (auto* service = process->services.FirstItem())
    ipc::UnregisterService(service);

  scheduling::CancelAllTimerEventsForProcess(process);
  scheduling::CancelTimeInfoChangeSubscriptionsForProcess(process);

  // The RPC lists are mutated by ipc/messages.cc under both processes' message
  // locks, so teardown has to take them too. Each entry is detached under the
  // locks and released outside them.
  while (true) {
    ipc::RPC* rpc = nullptr;
    {
      InterruptSafeSpinlockGuard guard(process->message_lock);
      rpc = process->rpcs_this_process_is_waiting_on.FirstItem();
    }
    if (rpc == nullptr) break;
    {
      ScopedTwoLocks locks(&process->message_lock, &rpc->callee->message_lock);
      process->rpcs_this_process_is_waiting_on.Remove(rpc);
      process->rpc_count--;
      rpc->callee->rpcs_waiting_on_this_process.Remove(rpc);
    }
    ObjectPool<ipc::RPC>::Release(rpc);
  }

  while (true) {
    ipc::RPC* rpc = nullptr;
    {
      InterruptSafeSpinlockGuard guard(process->message_lock);
      rpc = process->rpcs_waiting_on_this_process.FirstItem();
    }
    if (rpc == nullptr) break;

    Process* caller = rpc->caller;
    size_t response_message_id = rpc->response_message_id;
    {
      ScopedTwoLocks locks(&process->message_lock, &caller->message_lock);
      process->rpcs_waiting_on_this_process.Remove(rpc);
      caller->rpcs_this_process_is_waiting_on.Remove(rpc);
      caller->rpc_count--;
    }
    // Sent after the lists are consistent, because delivering a message takes
    // the caller's message lock.
    ipc::SendKernelRpcResponse(caller, response_message_id, process->pid,
                               (size_t)Status::PROCESS_DOESNT_EXIST);
    ObjectPool<ipc::RPC>::Release(rpc);
  }

  while (auto* shared_memory_in_process =
             process->joined_shared_memories.FirstItem())
    memory::UnmapSharedMemoryFromProcess(shared_memory_in_process);

  ipc::UnregisterAllSharedMemoryEventsForProcess(process);

  while (auto* notification =
             process->processes_i_want_to_be_notified_of_when_they_die
                 .FirstItem())
    ReleaseNotification(notification);

  while (auto* notification =
             process->processes_to_notify_when_i_die.FirstItem()) {
    ipc::SendKernelMessageToProcess(notification->notifyee,
                                    notification->event_id, process->pid, 0, 0,
                                    0, 0);
    ReleaseNotification(notification);
  }

#ifdef ENABLE_TRACING
  EmitProcessTerminatedTrace(process);
#endif

  // Released here rather than from ~Process(), so that freeing the page tables
  // (which waits for other cores to stop using them) happens on this core at a
  // known point, instead of on whichever core drops the last reference while
  // possibly holding unrelated locks.
  process->virtual_address_space.ReleaseAllMemory();

  // Drops the reference the process manager held. The process is freed here
  // unless another core is still holding a reference from a lookup.
  ReleaseProcessReference(*process);
}

void AcquireProcessReference(Process& process) {
  __atomic_fetch_add(&process.reference_count, 1, __ATOMIC_RELAXED);
}

void ReleaseProcessReference(Process& process) {
  if (__atomic_sub_fetch(&process.reference_count, 1, __ATOMIC_ACQ_REL) != 0)
    return;

  // DestroyProcess has already emptied everything, so this only runs the
  // destructors of empty containers and an already-released address space.
  process.~Process();
  free(&process);
}

ProcessRef ProcessManager::Find(size_t pid) {
  InterruptSafeSpinlockGuard guard(lock_);
  Process* process = pid < kMaxFastProcesses
                         ? processes_by_pid_[pid]
                         : all_processes_.SearchForItemEqualToValue(pid);
  // The reference is taken under the same lock that DestroyProcess uses to
  // unlink, so a process handed out here cannot be freed before the caller
  // releases it.
  if (process == nullptr || process->is_dying) return ProcessRef();
  AcquireProcessReference(*process);
  return ProcessRef(process);
}

ProcessRef ProcessManager::FindNext(size_t pid) {
  InterruptSafeSpinlockGuard guard(lock_);
  Process* process =
      all_processes_.SearchForItemGreaterThanOrEqualToValue(pid);
  while (process != nullptr && process->is_dying)
    process = all_processes_.NextItem(process);
  if (process == nullptr) return ProcessRef();
  AcquireProcessReference(*process);
  return ProcessRef(process);
}

size_t ProcessManager::QueryProcesses(const char* name, size_t min_pid,
                                      size_t* pids, size_t max_results) {
  InterruptSafeSpinlockGuard guard(lock_);
  Process* process =
      all_processes_.SearchForItemGreaterThanOrEqualToValue(min_pid);
  size_t processes_found = 0;
  while (process != nullptr) {
    if (name[0] == 0 || DoProcessNamesMatch(name, process->name)) {
      if (processes_found < max_results)
        pids[processes_found] = process->pid;
      processes_found++;
    }
    process = all_processes_.NextItem(process);
  }
  return processes_found;
}

bool ProcessManager::GetProcessName(size_t pid, char* name_out) {
  InterruptSafeSpinlockGuard guard(lock_);
  Process* process = all_processes_.SearchForItemEqualToValue(pid);
  if (process == nullptr) return false;
  // Only the name itself is copied. Callers pass a buffer of exactly
  // kProcessNameLength bytes, with no room for the null terminator.
  memcpy(name_out, process->name, kProcessNameLength);
  return true;
}

ProcessRef ProcessManager::FindNextWithName(const char* name, size_t min_pid) {
  InterruptSafeSpinlockGuard guard(lock_);
  Process* potential_process =
      all_processes_.SearchForItemGreaterThanOrEqualToValue(min_pid);
  while (potential_process != nullptr) {
    if (!potential_process->is_dying &&
        (name[0] == 0 || DoProcessNamesMatch(name, potential_process->name))) {
      AcquireProcessReference(*potential_process);
      return ProcessRef(potential_process);
    }
    potential_process = all_processes_.NextItem(potential_process);
  }
  return ProcessRef();
}

bool ProcessManager::HasRunningProcesses() {
  InterruptSafeSpinlockGuard guard(lock_);
  return !all_processes_.IsEmpty();
}

void ProcessManager::NotifyProcessOnDeath(Process* target, Process* notifyee,
                                         size_t event_id) {
  constexpr size_t kMaxDeathNotifications = 128;
  {
    InterruptSafeSpinlockGuard guard(notifyee->lock);
    size_t count = 0;
    for (auto* n : notifyee->processes_i_want_to_be_notified_of_when_they_die) {
      count++;
      if (count >= kMaxDeathNotifications) return;
    }
  }

  auto notification = ObjectPool<ProcessToNotifyOnExit>::Allocate();
  if (notification == nullptr) return;

  notification->target = target;
  notification->notifyee = notifyee;
  notification->event_id = event_id;

  {
    ScopedTwoLocks locks(&target->lock, &notifyee->lock);
    target->processes_to_notify_when_i_die.AddBack(notification);
    notifyee->processes_i_want_to_be_notified_of_when_they_die.AddBack(
        notification);
  }
}

void ProcessManager::StopNotifyingProcessOnDeath(Process* notifyee,
                                                size_t event_id) {
  while (true) {
    Process* target = nullptr;
    {
      InterruptSafeSpinlockGuard guard(notifyee->lock);
      for (auto* notification :
           notifyee->processes_i_want_to_be_notified_of_when_they_die) {
        if (notification->event_id == event_id) {
          target = notification->target;
          break;
        }
      }
    }
    if (target == nullptr) return;

    ScopedTwoLocks locks(&notifyee->lock, &target->lock);
    ProcessToNotifyOnExit* to_release = nullptr;
    for (auto* notification :
         notifyee->processes_i_want_to_be_notified_of_when_they_die) {
      if (notification->event_id == event_id && notification->target == target) {
        to_release = notification;
        target->processes_to_notify_when_i_die.Remove(notification);
        notifyee->processes_i_want_to_be_notified_of_when_they_die.Remove(
            notification);
        break;
      }
    }
    if (to_release != nullptr) {
      ObjectPool<ProcessToNotifyOnExit>::Release(to_release);
      return;
    }
  }
}

Process* ProcessManager::CreateChildProcess(Process* parent, char* name,
                                           size_t bitfield) {
  if (!parent->can_create_processes) return nullptr;

  // Non-driver processes cannot grant driver privileges.
  if (!parent->is_driver) bitfield &= ~(1 << 0);

  Process* child_process =
      CreateProcess(/*is_driver=*/bitfield & (1 << 0),
                    /*can_create_processes=*/bitfield & (1 << 1),
                    /*can_set_focus=*/bitfield & (1 << 2),
                    /*can_terminate_processes=*/bitfield & (1 << 3),
                    name);
  if (child_process == nullptr) {
    char safe_name[kProcessNameLength + 1];
    common::CopyString(name, kProcessNameLength, kProcessNameLength, safe_name);
    print << "Out of memory to create a new process: " << safe_name << '\n';
    return nullptr;
  }

  child_process->parent_pid = parent->pid;

  {
    InterruptSafeSpinlockGuard guard(parent->lock);
    child_process->next_child_process_in_parent = parent->child_processes;
    parent->child_processes = child_process;
    child_process->parent = parent;
  }

#ifdef ENABLE_TRACING
  EmitProcessCreatedTrace(child_process);
#endif
  return child_process;
}


bool ProcessManager::IsProcessAChildOfParent(Process* parent, Process* child) {
  if (child == nullptr) return false;
  InterruptSafeSpinlockGuard guard(parent->lock);
  Process* proc = parent->child_processes;
  while (proc != nullptr) {
    if (proc == child) return true;
    proc = proc->next_child_process_in_parent;
  }
  return false;
}

void ProcessManager::SetChildProcessMemoryPages(Process* parent, Process* child,
                                               size_t source_address,
                                               size_t destination_address,
                                               size_t page_count) {
  if (!IsProcessAChildOfParent(parent, child)) return;

  // The page count and both addresses come from userspace. The loop is bounded
  // because the kernel runs with interrupts disabled, so an overlong loop hangs
  // the core rather than just being slow.
  if (page_count == 0 || page_count > memory::kMaxPagesPerSyscall) return;

  size_t range_in_bytes = page_count * kPageSize;
  if (source_address + range_in_bytes < source_address ||
      destination_address + range_in_bytes < destination_address)
    return;

  for (size_t p = 0; p < page_count; p++) {
    size_t src = source_address + p * kPageSize;
    size_t dest = destination_address + p * kPageSize;

    size_t page_physical_address =
        parent->virtual_address_space.GetPhysicalAddress(
            src, /*ignore_unowned_pages=*/true);
    if (page_physical_address == kOutOfMemory) continue;

    if (!IsPageAlignedAddress(src)) {
      print << "SetChildProcessMemoryPages called with non page aligned "
               "source address: "
            << NumberFormat::Hexidecimal << src << '\n';
      src = RoundDownToPageAlignedAddress(src);
    }

    parent->virtual_address_space.ReleasePages(src, 1);

    if (!IsPageAlignedAddress(dest)) {
      print << "SetChildProcessMemoryPages called with non page aligned "
               "destination address: "
            << NumberFormat::Hexidecimal << dest << '\n';
      dest = RoundDownToPageAlignedAddress(dest);
    }

    if (!child->virtual_address_space.ReserveAddressRange(dest, 1)) {
      FreePhysicalPage(page_physical_address);
      continue;
    }

    child->virtual_address_space.MapPhysicalPageAt(
        dest, page_physical_address, /*own=*/true, true, false);
  }
}

void ProcessManager::StartExecutingChildProcess(Process* parent, Process* child,
                                               size_t entry_address,
                                               size_t params) {
  if (!RemoveChildProcessOfParent(parent, child)) return;

  Thread* thread = scheduling::CreateThread(child, entry_address, params);
  if (!thread) {
    print << "Out of memory to create the thread.\n";
    DestroyProcess(child);
    return;
  }

  scheduling::ScheduleThread(thread);
}

void ProcessManager::DestroyChildProcess(Process* parent, Process* child) {
  if (!RemoveChildProcessOfParent(parent, child)) return;
  DestroyProcess(child);
}

}  // namespace processes
