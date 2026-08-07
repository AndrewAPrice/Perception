#include "process.h"

#include "../../../Libraries/perception/public/perception/tracing.h"
#include "interrupts.h"
#include "kernel_string.h"
#include "heap_allocator.h"
#include "linked_list.h"
#include "messages.h"
#include "object_pool.h"
#include "physical_allocator.h"
#include "profiling.h"
#include "scheduler.h"
#include "service.h"
#include "text_terminal.h"
#include "thread.h"
#include "timer.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

#ifdef ENABLE_TRACING
namespace {

inline uint64 ReadRdtsc() {
  uint32 lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64)hi << 32) | lo;
}

}  // namespace

void EmitProcessCreatedTrace(Process* process) {
  if (!process) return;
  uint64 tsc = ReadRdtsc();
  uint32 pid = static_cast<uint32>(process->pid);
  uint8 len = static_cast<uint8>(strlen((char*)process->name));

  char packet[14];
  packet[0] = 0x07;  // PROCESS_CREATED opcode
  memcpy(&packet[1], (const char*)&tsc, 8);
  memcpy(&packet[9], (const char*)&pid, 4);
  packet[13] = static_cast<char>(len);

  ScopedPrintSource source(0, "Kernel", 2);
  for (size_t i = 0; i < 14; i++) print << packet[i];
  for (size_t i = 0; i < len; i++) print << process->name[i];
}

void EmitProcessTerminatedTrace(Process* process) {
  if (!process) return;
  uint64 tsc = ReadRdtsc();
  uint32 pid = static_cast<uint32>(process->pid);

  char packet[13];
  packet[0] = 0x08;  // PROCESS_TERMINATED opcode
  memcpy(&packet[1], (const char*)&tsc, 8);
  memcpy(&packet[9], (const char*)&pid, 4);

  ScopedPrintSource source(0, "Kernel", 2);
  for (size_t i = 0; i < 13; i++) print << packet[i];
}
#endif

namespace {

// The last assigned process ID.
size_t last_assigned_pid;

//  Tree of processes that are running.
AATree<Process, &Process::node_in_all_processes, &Process::pid> all_processes;

}  // namespace

// Initializes the internal structures for tracking processes.
void InitializeProcesses() {
  last_assigned_pid = 0;
  new (&all_processes)
      AATree<Process, &Process::node_in_all_processes, &Process::pid>();
}

// Creates a process, returns ERROR if there was an error.
Process* CreateProcess(bool is_driver, bool can_create_processes) {
  // Create a memory space for it.
  Process* proc = (Process*)malloc(sizeof(Process));
  if (proc == nullptr) return nullptr;  // Out of memory.

  new (proc) Process();
  proc->is_driver = is_driver;
  proc->can_create_processes = can_create_processes;

  // Assign a name and process ID.
  memset((char*)proc->name, 0, PROCESS_NAME_LENGTH + 1);  // Clear the name.
  last_assigned_pid++;
  proc->pid = last_assigned_pid;

  // Allocate an address space.
  if (!proc->virtual_address_space.InitializeUserSpace()) {
    free(proc);
    return nullptr;
  }

  // Various linked lists of that should be initialized to nullptr.
  proc->parent = nullptr;
  proc->child_processes = nullptr;
  proc->next_child_process_in_parent = nullptr;
  proc->messages_queued = 0;
  proc->service_count = 0;
  proc->rpc_count = 0;
  proc->next_synthetic_rpc_response_message_id = 0;
  proc->futex_wake_message_id = 0;

  // Threads.
  proc->thread_count = 0;

  // Profiling.
  proc->has_enabled_profiling = 0;
  proc->cycles_spent_executing_while_profiled = 0;

  // Initialize CPU usage statistics.
  proc->creation_timestamp = GetCurrentTimestampInMicroseconds();
  proc->last_updated_epoch = 0;
  proc->is_on_active_list_this_epoch = false;
  proc->tracking_cpu_usage = false;
  for (int c = 0; c < MAX_CORES; c++) {
    proc->cpu_time_in_current_epoch[c] = 0;
    proc->rolling_cpu_percentage[c] = 0;
  }

  // Add to the tree of running processes.
  all_processes.Insert(proc);
  return proc;
}

// Releases a ProcessToNotifyOnExit object and disconnects it from the
// linked lists.
void ReleaseNotification(ProcessToNotifyOnExit* notification) {
  // Remove from target.
  notification->target->processes_to_notify_when_i_die.Remove(notification);
  // Remove from notifyee.
  notification->notifyee->processes_i_want_to_be_notified_of_when_they_die
      .Remove(notification);
  ObjectPool<ProcessToNotifyOnExit>::Release(notification);
}

// Removes a child process of a parent, and returns true if the process was a
// non-nullptr child of the parent before removal.
bool RemoveChildProcessOfParent(Process* parent, Process* child) {
  if (child == nullptr) return false;

  if (parent->child_processes == nullptr)
    return false;  // Parent has no children.
  if (child->parent != parent) return false;

  // Check if the child is the first child of the parent.
  if (child == parent->child_processes) {
    // Remove from the start of the linked list.
    parent->child_processes = child->next_child_process_in_parent;
    child->parent = nullptr;
    return true;
  }

  // Iterate through the list starting from the second child.
  Process* previous_child = parent->child_processes;
  Process* child_in_parent = previous_child->next_child_process_in_parent;

  while (child_in_parent != nullptr) {
    if (child_in_parent == child) {
      // Found the child in the parent. Point the previous child to the next
      // child.
      previous_child->next_child_process_in_parent =
          child_in_parent->next_child_process_in_parent;
      child->parent = nullptr;
      return true;
    }

    previous_child = child_in_parent;
    child_in_parent = child_in_parent->next_child_process_in_parent;
  }

  // Couldn't find the child in the parent.
  return false;
}

// Destroys a process.
void DestroyProcess(Process* process) {
  // Destroy child processes that haven't started.
  while (process->child_processes != nullptr)
    DestroyProcess(process->child_processes);

  NotifyProfilerThatProcessExited(process);

  // Automatically unsubscribe from CPU tracking subscriptions.
  RemoveProcessFromCpuTracking(process);

  // Remove from the parent.
  if (process->parent) RemoveChildProcessOfParent(process->parent, process);

  // Destroy all threads.
  DestroyThreadsForProcess(process, true);

  // If this is the focused process, unfocus it, but after all threads are
  // destroyed to reduce the amount of work it does.
  if (GetFocusedProcess() == process) SetFocusedProcess(nullptr);

  UnregisterAllMessagesToForOnInterruptForProcess(process);

  while (!process->services_i_want_to_be_notified_of_when_they_appear.IsEmpty())
    StopNotifyingProcessWhenServiceAppears(
        process->services_i_want_to_be_notified_of_when_they_appear
            .FirstItem());

  while (
      !process->services_i_want_to_be_notified_of_when_they_disappear.IsEmpty())
    StopNotifyingProcessWhenServiceDisappears(
        process->services_i_want_to_be_notified_of_when_they_disappear
            .FirstItem());

  while (auto* service = process->services.FirstItem())
    UnregisterService(service);

  CancelAllTimerEventsForProcess(process);
  CancelTimeInfoChangeSubscriptionsForProcess(process);

  // Clean up pending RPCs.
  while (!process->rpcs_this_process_is_waiting_on.IsEmpty()) {
    RPC* rpc = process->rpcs_this_process_is_waiting_on.PopFront();
    process->rpc_count--;
    rpc->callee->rpcs_waiting_on_this_process.Remove(rpc);
    ObjectPool<RPC>::Release(rpc);
  }

  while (!process->rpcs_waiting_on_this_process.IsEmpty()) {
    RPC* rpc = process->rpcs_waiting_on_this_process.FirstItem();
    SendKernelRpcResponse(rpc->caller, rpc->response_message_id, process->pid,
                          (size_t)Status::PROCESS_DOESNT_EXIST);
    rpc->caller->rpcs_this_process_is_waiting_on.Remove(rpc);
    rpc->caller->rpc_count--;
    process->rpcs_waiting_on_this_process.Remove(rpc);
    ObjectPool<RPC>::Release(rpc);
  }

  // Release any shared memory mapped into this process.
  while (auto* shared_memory_in_process =
             process->joined_shared_memories.FirstItem())
    UnmapSharedMemoryFromProcess(shared_memory_in_process);

  // Free any shared memory events.
  UnregisterAllSharedMemoryEventsForProcess(process);

  // Free the address space.
  process->virtual_address_space.~VirtualAddressSpace();

  // Free all notifications being waited on for processes to die.
  while (auto* notification =
             process->processes_i_want_to_be_notified_of_when_they_die
                 .FirstItem()) {
    ReleaseNotification(notification);
  }

  // Notify the processes that were wanting to know when this process died.
  while (auto* notification =
             process->processes_to_notify_when_i_die.FirstItem()) {
    SendKernelMessageToProcess(notification->notifyee, notification->event_id,
                               process->pid, 0, 0, 0, 0);
    ReleaseNotification(notification);
  }

  // Remove from tree.
  all_processes.Remove(process);

#ifdef ENABLE_TRACING
  EmitProcessTerminatedTrace(process);
#endif

  // Free the process.
  free(process);
}

bool AreAnyProcessesRunning() { return !all_processes.IsEmpty(); }

// Registers that a process wants to be notified if another process dies.
void NotifyProcessOnDeath(Process* target, Process* notifyee, size_t event_id) {
  auto notification = ObjectPool<ProcessToNotifyOnExit>::Allocate();
  if (notification == nullptr) return;

  notification->target = target;
  notification->notifyee = notifyee;
  notification->event_id = event_id;

  target->processes_to_notify_when_i_die.AddBack(notification);
  notifyee->processes_i_want_to_be_notified_of_when_they_die.AddBack(
      notification);
}

void StopNotifyingProcessOnDeath(Process* notifyee, size_t event_id) {
  // Find the notification.
  for (auto notification :
       notifyee->processes_i_want_to_be_notified_of_when_they_die) {
    if (notification->event_id == event_id)
      return ReleaseNotification(notification);
  }
}

// Returns a process with the provided pid, returns nullptr if it doesn't
// exist.
Process* GetProcessFromPid(size_t pid) {
  return all_processes.SearchForItemEqualToValue(pid);
}

// Returns a process with the provided pid, and if it doesn't exist, returns
// the process with the next highest pid. Returns nullptr if no process exists
// with a pid >= pid.
Process* GetProcessOrNextFromPid(size_t pid) {
  return all_processes.SearchForItemGreaterThanOrEqualToValue(pid);
}

// Do two process names (of length PROCESS_NAME_LENGTH) match?
bool DoProcessNamesMatch(const char* a, const char* b) {
  for (int word = 0; word < PROCESS_NAME_WORDS; word++)
    if (((size_t*)a)[word] != ((size_t*)b)[word]) return false;

  return true;
}

// Returns the next process with the given name (which must be an array of
// length PROCESS_NAME_LENGTH). last_process may be nullptr if you want to fetch
// the first process with the name. Returns nullptr if there are no more
// processes with the provided name.
Process* FindNextProcessWithName(const char* name, Process* start_from) {
  Process* potential_process =
      start_from == nullptr ? all_processes.FirstItem() : start_from;
  // Loop over every process.
  while (potential_process != nullptr) {
    if (name[0] == 0 || DoProcessNamesMatch(name, potential_process->name))
      // We found a process with this name!
      return potential_process;
    // Try the next process.
    potential_process = all_processes.NextItem(potential_process);
  }

  // No process was found with the name.
  return nullptr;
}

// Creates a child process. The parent process must be allowed to create
// children. Returns ERROR if there was an error.
Process* CreateChildProcess(Process* parent, char* name, size_t bitfield) {
  if (!parent->can_create_processes) return nullptr;
  Process* child_process =
      CreateProcess(/*is_driver=*/bitfield & (1 << 0),
                    /*can_create_processes=*/bitfield & (1 << 2));
  if (child_process == nullptr) {
    print << "Out of memory to create a new process: " << name << '\n';
    return nullptr;
  }

  // Add to the linked list of children in the parent.
  child_process->next_child_process_in_parent = parent->child_processes;
  parent->child_processes = child_process;
  child_process->parent = parent;

  CopyString((char*)name, PROCESS_NAME_LENGTH, PROCESS_NAME_LENGTH,
             (char*)child_process->name);
#ifdef ENABLE_TRACING
  EmitProcessCreatedTrace(child_process);
#endif
  return child_process;
}

bool IsProcessAChildOfParent(Process* parent, Process* child) {
  if (child == nullptr) return false;
  Process* proc = parent->child_processes;
  while (proc != nullptr) {
    if (proc == child) return true;
    proc = proc->next_child_process_in_parent;
  }
  return false;
}

// Unmaps memory pages from the parent and assigns them to the child. The memory
// is unmapped from the calling process regardless of if this call succeeds. If
// the page already exists in the child process, nothing is set.
void SetChildProcessMemoryPages(Process* parent, Process* child,
                                size_t source_address,
                                size_t destination_address, size_t page_count) {
  if (!IsProcessAChildOfParent(parent, child)) return;

  for (size_t p = 0; p < page_count; p++) {
    size_t src = source_address + p * PAGE_SIZE;
    size_t dest = destination_address + p * PAGE_SIZE;

    // Get the physical address from the parent.
    size_t page_physical_address =
        parent->virtual_address_space.GetPhysicalAddress(
            src,
            /*ignore_unowned_pages=*/true);
    if (page_physical_address == OUT_OF_MEMORY) {
      continue;  // Page doesn't exist.
    }

    if (!IsPageAlignedAddress(src)) {
      print << "SetChildProcessMemoryPages called with non page aligned "
               "source address: "
            << NumberFormat::Hexidecimal << src << '\n';
      src = RoundDownToPageAlignedAddress(src);
    }

    // Unmap the physical page from the parent.
    parent->virtual_address_space.ReleasePages(src, 1);

    if (!IsPageAlignedAddress(dest)) {
      print << "SetChildProcessMemoryPages called with non page aligned "
               "destination address: "
            << NumberFormat::Hexidecimal << dest << '\n';
      dest = RoundDownToPageAlignedAddress(dest);
    }

    if (!child->virtual_address_space.ReserveAddressRange(dest, 1)) {
      // There's no free memory at this address. Release the memory for this
      // page.
      FreePhysicalPage(page_physical_address);
      continue;
    }

    // Map the physical page to the new process.
    child->virtual_address_space.MapPhysicalPageAt(dest, page_physical_address,
                                                   /*own=*/true, true, false);
  }
}

// Creates a thread in the a process that is currently in the `creating` state.
// The child process will no longer be in the `creating` state. The calling
// process must be the child process's creator. The child process will begin
// executing and will no longer terminate if the creator terminates.
void StartExecutingChildProcess(Process* parent, Process* child,
                                size_t entry_address, size_t params) {
  if (!RemoveChildProcessOfParent(parent, child)) return;

  Thread* thread = CreateThread(child, entry_address, params);

  if (!thread) {
    print << "Out of memory to create the thread.\n";
    DestroyProcess(child);
    return;
  }

  ScheduleThread(thread);
}

// Destroys a process in the `creating` state.
void DestroyChildProcess(Process* parent, Process* child) {
  if (!RemoveChildProcessOfParent(parent, child)) return;
  DestroyProcess(child);
}

Process* GetNextProcess(Process* process) {
  if (process == nullptr) return all_processes.FirstItem();
  return all_processes.NextItem(process);
}

void AwakeFutexInProcess(Process* process, size_t address) {
  if (!process->futex_wake_message_id) return;

  if (process->futex_wake_message_id != 0) {
    SendKernelMessageToProcess(process, process->futex_wake_message_id,
                               /*param1=*/address, /*param2=*/0,
                               /*param3=*/0,
                               /*param4=*/0, /*param5=*/0);
  }
}
