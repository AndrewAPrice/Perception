#include "process.h"

#include "interrupts.h"
#include "io.h"
#include "liballoc.h"
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
#include "virtual_allocator.h"

namespace {

// The last assigned process ID.
size_t last_assigned_pid;

//  Linked list of processes that are running.
LinkedList<Process, &Process::node_in_all_processes> all_processes;

}  // namespace

// Initializes the internal structures for tracking processes.
void InitializeProcesses() {
  last_assigned_pid = 0;
  new (&all_processes) LinkedList<Process, &Process::node_in_all_processes>();
}

// Creates a process, returns ERROR if there was an error.
Process *CreateProcess(bool is_driver, bool can_create_processes) {
  // Create a memory space for it.
  Process *proc = (Process *)malloc(sizeof(Process));
  if (proc == 0) {
    // Out of memory.
    return (Process *)ERROR;
  }
  new (proc) Process();
  proc->is_driver = is_driver;
  proc->can_create_processes = can_create_processes;

  // Assign a name and process ID.
  memset((char *)proc->name, 0, PROCESS_NAME_LENGTH);  // Clear the name.
  last_assigned_pid++;
  proc->pid = last_assigned_pid;

  // Allocate an address space.
  if (!InitializeVirtualAddressSpace(&proc->virtual_address_space)) {
    free(proc);
    return (Process *)ERROR;
  }
  proc->allocated_pages = 0;

  // Various linked lists of that should be initialized to nullptr.
  proc->parent = nullptr;
  proc->child_processes = nullptr;
  proc->next_child_process_in_parent = nullptr;
  proc->messages_queued = 0;
  proc->thread_sleeping_for_message = nullptr;
  proc->message_to_fire_on_interrupt = nullptr;
  proc->first_service = nullptr;
  proc->last_service = nullptr;
  proc->timer_event = nullptr;

  // Threads.
  proc->threads = 0;
  proc->thread_count = 0;

  // Profiling.
  proc->has_enabled_profiling = 0;
  proc->cycles_spent_executing_while_profiled = 0;

  // Add to the linked list of running processes.
  all_processes.AddBack(proc);
  return proc;
}

// Releases a ProcessToNotifyOnExit object and disconnects it from the
// linked lists.
void ReleaseNotification(ProcessToNotifyOnExit *notification) {
  // Remove from target.
  notification->target->processes_to_notify_when_i_die.Remove(notification);
  notification->target->processes_i_want_to_be_notified_of_when_they_die.Remove(
      notification);
  ObjectPool<ProcessToNotifyOnExit>::Release(notification);
}

// Removes a child process of a parent, and returns true if the process was a
// non-nullptr child of the parent before removal.
bool RemoveChildProcessOfParent(Process *parent, Process *child) {
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
  Process *previous_child = parent->child_processes;
  Process *child_in_parent = previous_child->next_child_process_in_parent;

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
void DestroyProcess(Process *process) {
  // Destroy child processes that haven't started.
  while (process->child_processes != nullptr) {
    DestroyProcess(process->child_processes);
  }

  NotifyProfilerThatProcessExited(process);

  // Remove from the parent.
  if (process->parent) RemoveChildProcessOfParent(process->parent, process);

  // Destroy all threads.
  DestroyThreadsForProcess(process, true);

  if (process->message_to_fire_on_interrupt != nullptr)
    UnregisterAllMessagesToForOnInterruptForProcess(process);

  while (process->services_i_want_to_be_notified_of_when_they_appear != nullptr)
    StopNotifyingProcessWhenServiceAppears(
        process->services_i_want_to_be_notified_of_when_they_appear);

  while (process->first_service != nullptr)
    UnregisterService(process->first_service);

  if (process->timer_event != nullptr) CancelAllTimerEventsForProcess(process);

  // Release any shared memory mapped into this process.
  while (!process->joined_shared_memories.IsEmpty())
    UnmapSharedMemoryFromProcess(process->joined_shared_memories.FirstItem());

  // Free the address space.
  FreeAddressSpace(&process->virtual_address_space);

  // Free all notifications I was waiting on for processes to die.
  while (auto *notification =
             process->processes_i_want_to_be_notified_of_when_they_die
                 .FirstItem()) {
    ReleaseNotification(notification);
  }

  // Notify the processes that were wanting to know when this process died.
  while (auto *notification =
             process->processes_i_want_to_be_notified_of_when_they_die
                 .FirstItem()) {
    SendKernelMessageToProcess(notification->notifyee, notification->event_id,
                               process->pid, 0, 0, 0, 0);
    ReleaseNotification(notification);
  }

  // Remove from linked list.
  all_processes.Remove(process);

  // Free the process.
  free(process);
}

// Registers that a process wants to be notified if another process dies.
extern void NotifyProcessOnDeath(Process *target, Process *notifyee,
                                 size_t event_id) {
  auto notification = ObjectPool<ProcessToNotifyOnExit>::Allocate();
  if (notification == nullptr) return;

  notification->target = target;
  notification->notifyee = notifyee;
  notification->event_id = event_id;

  target->processes_to_notify_when_i_die.AddBack(notification);
  notifyee->processes_i_want_to_be_notified_of_when_they_die.AddBack(
      notification);
}

// Returns a process with the provided pid, returns nullptr if it doesn't
// exist.
Process *GetProcessFromPid(size_t pid) {
  // Walk through the linked list to find our process.
  for (Process *proc : all_processes)
    if (proc->pid == pid) return proc;

  return (Process *)nullptr;
}

// Returns a process with the provided pid, and if it doesn't exist, returns
// the process with the next highest pid. Returns nullptr if no process exists
// with a pid >= pid.
Process *GetProcessOrNextFromPid(size_t pid) {
  // Walk through the linked list to find our process.
  for (Process *proc : all_processes)
    if (proc->pid >= pid) return proc;

  return (Process *)nullptr;
}

// Do two process names (of length PROCESS_NAME_LENGTH) match?
bool DoProcessNamesMatch(const char *a, const char *b) {
  for (int word = 0; word < PROCESS_NAME_WORDS; word++)
    if (((size_t *)a)[word] != ((size_t *)b)[word]) return false;

  return true;
}

// Returns the next process with the given name (which must be an array of
// length PROCESS_NAME_LENGTH). last_process may be nullptr if you want to fetch
// the first process with the name. Returns nullptr if there are no more
// processes with the provided name.
Process *FindNextProcessWithName(const char *name, Process *start_from) {
  Process *potential_process = start_from;
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
Process *CreateChildProcess(Process *parent, char *name, size_t bitfield) {
  if (!parent->can_create_processes) return nullptr;
  Process *child_process =
      CreateProcess(/*is_driver=*/bitfield & (1 << 0),
                    /*can_create_processes=*/bitfield & (1 << 2));
  if (child_process == (Process *)ERROR) {
    print << "Out of memory to create a new process: " << name << '\n';
    return nullptr;
  }

  // Add to the linked list of children in the parent.
  child_process->next_child_process_in_parent = parent->child_processes;
  parent->child_processes = child_process;
  child_process->parent = parent;

  CopyString((char *)name, PROCESS_NAME_LENGTH, PROCESS_NAME_LENGTH,
             (char *)child_process->name);
  return child_process;
}

// Returns if a process is a child of a parent. Also returns false if the child
// is nullptr.
bool IsProcessAChildOfParent(Process *parent, Process *child) {
  if (child == nullptr) return false;
  Process *proc = parent->child_processes;
  while (proc != nullptr) {
    if (proc == child) return true;
    proc = proc->next_child_process_in_parent;
  }
  return false;
}

// Unmaps a memory page from the parent and assigns it to the child. The memory
// is unmapped from the calling process regardless of if this call succeeds. If
// the page already exists in the child process, nothing is set.
void SetChildProcessMemoryPage(Process *parent, Process *child,
                               size_t source_address,
                               size_t destination_address) {
  // Get the physical address from the parent.
  size_t page_physical_address =
      GetPhysicalAddress(&parent->virtual_address_space, source_address,
                         /*ignore_unowned_pages=*/true);
  if (page_physical_address == OUT_OF_MEMORY) {
    return;  // Page doesn't exist.
  }

  // Unmap the physical page from the parent.
  UnmapVirtualPage(&parent->virtual_address_space, source_address, false);

  if (!IsProcessAChildOfParent(parent, child)) {
    // This isn't a child process. Release the memory for this page.
    FreePhysicalPage(page_physical_address);
    return;
  }

  // Map the physical page to the new process.
  MapPhysicalPageToVirtualPage(&child->virtual_address_space,
                               destination_address, page_physical_address,
                               /*own=*/true, true, false);
}

// Creates a thread in the a process that is currently in the `creating` state.
// The child process will no longer be in the `creating` state. The calling
// process must be the child process's creator. The child process will begin
// executing and will no longer terminate if the creator terminates.
void StartExecutingChildProcess(Process *parent, Process *child,
                                size_t entry_address, size_t params) {
  if (!RemoveChildProcessOfParent(parent, child)) return;

  Thread *thread = CreateThread(child, entry_address, params);

  if (!thread) {
    print << "Out of memory to create the thread.\n";
    DestroyProcess(child);
    return;
  }

  ScheduleThread(thread);
}

// Destroys a process in the `creating` state.
void DestroyChildProcess(Process *parent, Process *child) {
  if (!RemoveChildProcessOfParent(parent, child)) return;
  DestroyProcess(child);
}

Process *GetNextProcess(Process *process) {
  if (process == nullptr)
    return all_processes.FirstItem();

  return all_processes.NextItem(process);
}
