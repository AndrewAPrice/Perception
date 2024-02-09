#include "process.h"

#include "interrupts.h"
#include "io.h"
#include "liballoc.h"
#include "messages.h"
#include "object_pool.h"
#include "physical_allocator.h"
#include "service.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "thread.h"
#include "timer.h"
#include "virtual_allocator.h"

// The last assigned process ID.
size_t last_assigned_pid;

//  Linked list of processes that are running.
struct Process *first_process;
struct Process *last_process;

// Initializes the internal structures for tracking processes.
void InitializeProcesses() {
  last_assigned_pid = 0;
  first_process = nullptr;
  last_process = nullptr;
}

// Creates a process, returns ERROR if there was an error.
struct Process *CreateProcess(bool is_driver, bool can_create_processes) {
  // Create a memory space for it.
  struct Process *proc = (struct Process*)malloc(sizeof(struct Process));
  if (proc == 0) {
    // Out of memory.
    return (struct Process *)ERROR;
  }
  proc->is_driver = is_driver;
  proc->can_create_processes = can_create_processes;

  // Assign a name and process ID.
  memset((char *)proc->name, 0, PROCESS_NAME_LENGTH);  // Clear the name.
  last_assigned_pid++;
  proc->pid = last_assigned_pid;

  // Allocate an address space.
  if (!InitializeVirtualAddressSpace(&proc->virtual_address_space)) {
    free(proc);
    return (struct Process *)ERROR;
  }
  proc->allocated_pages = 0;

  // Various linked lists of that should be initialized to nullptr.
  proc->parent = nullptr;
  proc->child_processes = nullptr;
  proc->next_child_process_in_parent = nullptr;
  proc->next_message = nullptr;
  proc->last_message = nullptr;
  proc->messages_queued = 0;
  proc->thread_sleeping_for_message = nullptr;
  proc->message_to_fire_on_interrupt = nullptr;
  proc->processes_to_notify_when_i_die = nullptr;
  proc->processes_i_want_to_be_notified_of_when_they_die = nullptr;
  proc->services_i_want_to_be_notified_of_when_they_appear = nullptr;
  proc->first_service = nullptr;
  proc->last_service = nullptr;
  proc->shared_memory = nullptr;
  proc->timer_event = nullptr;

  // Threads.
  proc->threads = 0;
  proc->thread_count = 0;

  // Add to the linked list of running processes.
  if (first_process == nullptr) {
    // No running processes.
    first_process = proc;
    last_process = proc;
    proc->previous = nullptr;
  } else {
    last_process->next = proc;
    proc->previous = last_process;
    last_process = proc;
  }

  proc->next = nullptr;

  return proc;
}

// Releases a ProcessToNotifyOnExit object and disconnects it from the
// linked lists.
void ReleaseNotification(struct ProcessToNotifyOnExit *notification) {
  // Remove from target.
  if (notification->previous_in_target == nullptr) {
    notification->target->processes_to_notify_when_i_die =
        notification->next_in_target;
  } else {
    notification->previous_in_target->next_in_target =
        notification->next_in_target;
  }

  if (notification->next_in_target != nullptr) {
    notification->next_in_target->previous_in_target =
        notification->previous_in_target;
  }

  // Remove from notifyee.
  if (notification->previous_in_notifyee == nullptr) {
    notification->notifyee->processes_i_want_to_be_notified_of_when_they_die =
        notification->next_in_notifyee;
  } else {
    notification->previous_in_notifyee->next_in_notifyee =
        notification->next_in_notifyee;
  }

  if (notification->next_in_notifyee != nullptr) {
    notification->next_in_notifyee->previous_in_notifyee =
        notification->previous_in_notifyee;
  }

  ObjectPool<ProcessToNotifyOnExit>::Release(notification);
}

// Removes a child process of a parent, and returns true if the process was a
// non-nullptr child of the parent before removal.
bool RemoveChildProcessOfParent(struct Process *parent, struct Process *child) {
  if (child == nullptr) return false;

  if (parent->child_processes == nullptr) return false;  // Parent has no children.
  if (child->parent != parent) return false;

  // Check if the child is the first child of the parent.
  if (child == parent->child_processes) {
    // Remove from the start of the linked list.
    parent->child_processes = child->next_child_process_in_parent;
    child->parent = nullptr;
    return true;
  }

  // Iterate through the list starting from the second child.
  struct Process *previous_child = parent->child_processes;
  struct Process *child_in_parent =
      previous_child->next_child_process_in_parent;

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
void DestroyProcess(struct Process *process) {
  // Destroy child processes that haven't started.
  while (process->child_processes != nullptr) {
    DestroyProcess(process->child_processes);
  }

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
  while (process->shared_memory != nullptr)
    UnmapSharedMemoryFromProcess(process, process->shared_memory);

  // Free the address space.
  FreeAddressSpace(&process->virtual_address_space);

  // Free all notifications I was waiting on for processes to die.
  while (process->processes_i_want_to_be_notified_of_when_they_die != nullptr) {
    ReleaseNotification(
        process->processes_i_want_to_be_notified_of_when_they_die);
  }

  // Notify the processes that were wanting to know when this process died.
  while (process->processes_to_notify_when_i_die != nullptr) {
    SendKernelMessageToProcess(
        process->processes_to_notify_when_i_die->notifyee,
        process->processes_to_notify_when_i_die->event_id, process->pid, 0, 0,
        0, 0);
    ReleaseNotification(process->processes_to_notify_when_i_die);
  }

  // Remove from linked list.
  if (process->previous == nullptr) {
    first_process = process->next;
  } else {
    process->previous->next = process->next;
  }

  if (process->next == nullptr) {
    last_process = process->previous;
  } else {
    process->next->previous = process->previous;
  }

  // Free the process.
  free(process);
}

// Registers that a process wants to be notified if another process dies.
extern void NotifyProcessOnDeath(struct Process *target,
                                 struct Process *notifyee, size_t event_id) {
  auto notification = ObjectPool<ProcessToNotifyOnExit>::Allocate();
  if (notification == nullptr) return;

  notification->target = target;
  notification->notifyee = notifyee;
  notification->event_id = event_id;

  notification->previous_in_target = nullptr;
  notification->next_in_target = target->processes_to_notify_when_i_die;
  target->processes_to_notify_when_i_die = notification;

  notification->previous_in_notifyee = nullptr;
  notification->next_in_notifyee =
      notifyee->processes_i_want_to_be_notified_of_when_they_die;
  notifyee->processes_i_want_to_be_notified_of_when_they_die = notification;
}

// Returns a process with the provided pid, returns nullptr if it doesn't
// exist.
struct Process *GetProcessFromPid(size_t pid) {
  // Walk through the linked list to find our process.
  for (struct Process *proc = first_process; proc != nullptr; proc = proc->next)
    if (proc->pid == pid) return proc;

  return (struct Process *)nullptr;
}

// Returns a process with the provided pid, and if it doesn't exist, returns
// the process with the next highest pid. Returns nullptr if no process exists
// with a pid >= pid.
struct Process *GetProcessOrNextFromPid(size_t pid) {
  // Walk through the linked list to find our process.
  struct Process *proc = first_process;
  for (struct Process *proc = first_process; proc != nullptr; proc = proc->next) {
    if (proc->pid >= pid) return proc;
  }

  return (struct Process *)nullptr;
}

// Do two process names (of length PROCESS_NAME_LENGTH) match?
bool DoProcessNamesMatch(const char *a, const char *b) {
  for (int word = 0; word < PROCESS_NAME_WORDS; word++)
    if (((size_t *)a)[word] != ((size_t *)b)[word]) return false;

  return true;
}

// Returns the next process with the given name (which must be an array of
// length PROCESS_NAME_LENGTH). last_process may be nullptr if you want to fetch
// the first process with the name. Returns nullptr if there are no more processes
// with the provided name.
struct Process *FindNextProcessWithName(const char *name,
                                        struct Process *start_from) {
  struct Process *potential_process = start_from;
  // Loop over every process.
  while (potential_process != nullptr) {
    if (name[0] == 0 || DoProcessNamesMatch(name, potential_process->name))
      // We found a process with this name!
      return potential_process;
    // Try the next process.
    potential_process = potential_process->next;
  }

  // No process was found with the name.
  return nullptr;
}

// Creates a child process. The parent process must be allowed to create
// children. Returns ERROR if there was an error.
struct Process *CreateChildProcess(struct Process *parent, char *name,
                                   size_t bitfield) {
  if (!parent->can_create_processes) return nullptr;
  struct Process *child_process =
      CreateProcess(/*is_driver=*/bitfield & (1 << 0),
                    /*can_create_processes=*/bitfield & (1 << 2));
  if (child_process == (struct Process *)ERROR) {
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
bool IsProcessAChildOfParent(struct Process *parent, struct Process *child) {
  if (child == nullptr) return false;
  struct Process *proc = parent->child_processes;
  while (proc != nullptr) {
    if (proc == child) return true;
    proc = proc->next_child_process_in_parent;
  }
  return false;
}

// Unmaps a memory page from the parent and assigns it to the child. The memory
// is unmapped from the calling process regardless of if this call succeeds. If
// the page already exists in the child process, nothing is set.
void SetChildProcessMemoryPage(struct Process *parent, struct Process *child,
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
void StartExecutingChildProcess(struct Process *parent, struct Process *child,
                                size_t entry_address, size_t params) {
  if (!RemoveChildProcessOfParent(parent, child)) return;

  struct Thread *thread = CreateThread(child, entry_address, params);

  if (!thread) {
    print << "Out of memory to create the thread.\n";
    DestroyProcess(child);
    return;
  }

  ScheduleThread(thread);
}

// Destroys a process in the `creating` state.
void DestroyChildProcess(struct Process *parent, struct Process *child) {
  if (!RemoveChildProcessOfParent(parent, child)) return;
  DestroyProcess(child);
}
