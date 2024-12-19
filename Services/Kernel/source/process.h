#pragma once

#include "interrupts.h"
#include "linked_list.h"
#include "messages.h"
#include "service.h"
#include "shared_memory.h"
#include "thread.h"
#include "timer_event.h"
#include "types.h"
#include "virtual_address_space.h"

#define PROCESS_NAME_WORDS 11
#define PROCESS_NAME_LENGTH (PROCESS_NAME_WORDS * 8)

struct MessageToFireOnInterrupt;
struct Message;
struct Process;
struct ProcessToNotifyWhenServiceAppears;
struct Service;
struct SharedMemoryInProcess;
struct TimerEvent;

struct ProcessToNotifyOnExit {
  // The process to trigger a message for when it dies.
  Process* target;

  // The process to notify when the above process dies.
  Process* notifyee;

  // The ID of the notification message to send to notifyee.
  size_t event_id;

  // Linked list of notification messages within the target process.
  LinkedListNode target_node;

  // Linked list of notification messages within the notifyee process.
  LinkedListNode notifyee_node;
};

struct Process {
  // Unique ID to identify this process.
  size_t pid;

  // Name of the process.
  char name[PROCESS_NAME_LENGTH + 1];

  // Is this a process a driver? Drivers have permission to do IO.
  bool is_driver;

  // Is this process allowed to create other processes?
  bool can_create_processes;

  // The parent of the current process. Only set if the process is in the
  // `creator` state.
  Process* parent;
  // A linked list of child processes in the `creator` state.
  Process* child_processes;
  // The next child process in a linked list in the parent.
  Process* next_child_process_in_parent;

  // The virtual address space that is unique to this process.
  VirtualAddressSpace virtual_address_space;

  // The number of allocated pages.
  size_t allocated_pages;  // The number of allocated pages.

  // Queued messages sent to this process, waiting to be consumed.
  LinkedList<Message, &Message::node> queued_messages;

  // Number of messages queued.
  size_t messages_queued;
  // Linked queue of threads that are currently sleeping and waiting for a
  // message.
  LinkedList<Thread, &Thread::node_sleeping_for_messages>
      threads_sleeping_for_message;

  // Linked list of messages to fire on an interrupt.
  LinkedList<MessageToFireOnInterrupt,
             &MessageToFireOnInterrupt::node_in_process>
      messages_to_fire_on_interrupt;

  // Linked list of threads.
  LinkedList<Thread, &Thread::node_in_process> threads;
  // Number of threads this process has.
  unsigned short thread_count;

  // Linked list of processes.
  LinkedListNode node_in_all_processes;

  // Linked lists of processes to notify when I die.
  LinkedList<ProcessToNotifyOnExit, &ProcessToNotifyOnExit::target_node>
      processes_to_notify_when_i_die;
  // Linked lists of processes I want to be notified of when they die.
  LinkedList<ProcessToNotifyOnExit, &ProcessToNotifyOnExit::notifyee_node>
      processes_i_want_to_be_notified_of_when_they_die;
  // Linked list of services I want to be notified of when they appear.
  LinkedList<ProcessToNotifyWhenServiceAppears,
             &ProcessToNotifyWhenServiceAppears::node_in_process>
      services_i_want_to_be_notified_of_when_they_appear;
  // Linked list of services I want to be notified of when they disappear.
  LinkedList<ProcessToNotifyWhenServiceDisappears,
             &ProcessToNotifyWhenServiceDisappears::node_in_process>
      services_i_want_to_be_notified_of_when_they_disappear;

  // Linked list of services in this process. System calls that scan for
  // services expect that services are added to the back of the list, and we
  // must iterate them from front to back.
  LinkedList<Service, &Service::node_in_process> services;

  // Linked list of shared memory mapped into this process.
  LinkedList<SharedMemoryInProcess, &SharedMemoryInProcess::node_in_process>
      joined_shared_memories;

  // Linked list of timer events that are scheduled for this process.
  LinkedList<TimerEvent, &TimerEvent::node_in_process> timer_events;

  // Whether this process has enabled profiling. This is actually a count
  // because the calls to enable profiling are nested.
  size_t has_enabled_profiling;

  // The number of CPU cycles spent executing this process while it has been
  // profiled.
  size_t cycles_spent_executing_while_profiled;
};

// Initializes the internal structures for tracking processes.
void InitializeProcesses();

// Creates a process, returns ERROR if there was an error.
Process* CreateProcess(bool is_driver, bool can_create_processes);

// Destroys a process - DO NOT CALL THIS DIRECTLY, destroy a process by
// destroying all of it's threads!
void DestroyProcess(Process* process);

// Registers that a process wants to be notified if another process dies.
void NotifyProcessOnDeath(Process* target, Process* notifyee, size_t event_id);

// Unregisters that a process wants to be notified if another process dies.
void StopNotifyingProcessOnDeath(Process* notifyee, size_t event_id);

// Returns a process with the provided pid, returns nullptr if it doesn't exist.
Process* GetProcessFromPid(size_t pid);

// Returns a process with the provided pid, and if it doesn't exist, returns
// the process with the next highest pid. Returns nullptr if no process exists
// with a pid >= pid.
Process* GetProcessOrNextFromPid(size_t pid);

// Returns the next process with the given name (which must be an array of
// length PROCESS_NAME_LENGTH). Returns nullptr if there are no more processes
// with the provided name. `start_from` is inclusive.
Process* FindNextProcessWithName(const char* name, Process* start_from);

// Creates a child process. The parent process must be allowed to create
// children. Returns ERROR if there was an error.
Process* CreateChildProcess(Process* parent, char* name, size_t bitfield);

// Unmaps a memory page from the parent and assigns it to the child. The memory
// is unmapped from the calling process regardless of if this call succeeds. If
// the page already exists in the child process, nothing is set.
void SetChildProcessMemoryPage(Process* parent, Process* child,
                               size_t source_address,
                               size_t destination_address);

// Creates a thread in the a process that is currently in the `creating` state.
// The child process will no longer be in the `creating` state. The calling
// process must be the child process's creator. The child process will begin
// executing and will no longer terminate if the creator terminates.
void StartExecutingChildProcess(Process* parent, Process* child,
                                size_t entry_address, size_t params);

// Destroys a process in the `creating` state.
void DestroyChildProcess(Process* parent, Process* child);

// Returns the next process in the system when iterating through all running
// processes.
Process* GetNextProcess(Process* process);

// Returns if a process is a child of a parent. Also returns false if the child
// is nullptr.
bool IsProcessAChildOfParent(Process* parent, Process* child);
