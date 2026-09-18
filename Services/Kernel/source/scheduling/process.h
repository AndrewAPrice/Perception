#pragma once

#include "containers/aa_tree.h"
#include "containers/linked_list.h"
#include "containers/spinlock.h"
#include "interrupts/interrupts.h"
#include "ipc/messages.h"
#include "ipc/rpc.h"
#include "ipc/service.h"
#include "ipc/shared_memory.h"
#include "ipc/shared_memory_event.h"
#include "memory/virtual_address_space.h"
#include "scheduling/per_cpu.h"
#include "scheduling/thread.h"
#include "scheduling/timer_event.h"
#include "types.h"

namespace scheduling {

// Length of a process name in 64-bit words.
constexpr size_t kProcessNameWords = 10;

// Maximum length of a process name in characters.
constexpr size_t kProcessNameLength = kProcessNameWords * 8;

struct Process;

struct ProcessToNotifyOnExit {
  // The process to trigger a message for when it dies.
  Process* target;

  // The process to notify when the above process dies.
  Process* notifyee;

  // The ID of the notification message to send to notifyee.
  size_t event_id;

  // Linked list of notification messages within the target process.
  containers::LinkedListNode target_node;

  // Linked list of notification messages within the notifyee process.
  containers::LinkedListNode notifyee_node;
};

struct Process {
  // Spinlock protecting process metadata, threads, services, and child
  // processes.
  containers::InterruptSafeSpinlock lock;

  // Spinlock protecting message queues, sleeping threads, and RPC tracking.
  containers::InterruptSafeSpinlock message_lock;

  // Unique ID to identify this process.
  size_t pid;

  // Name of the process.
  char name[kProcessNameLength + 1];

  // Is this process a driver? Drivers have permission to do IO.
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
  memory::VirtualAddressSpace virtual_address_space;

  // Queued messages sent to this process, waiting to be consumed.
  containers::LinkedList<ipc::Message, &ipc::Message::node> queued_messages;

  // Number of messages queued.
  size_t messages_queued;
  // Linked queue of threads that are currently sleeping and waiting for a
  // message.
  containers::LinkedList<Thread, &Thread::node_sleeping_for_messages>
      threads_sleeping_for_message;

  // Linked list of messages to fire on an interrupt.
  containers::LinkedList<interrupts::MessageToFireOnInterrupt,
                         &interrupts::MessageToFireOnInterrupt::node_in_process>
      messages_to_fire_on_interrupt;

  // Tree of threads.
  containers::AATree<Thread, &Thread::node_in_process, &Thread::id> threads;
  // Number of threads this process has.
  unsigned short thread_count;

  // Tree node of processes.
  containers::AATreeNode node_in_all_processes;

  // Linked lists of processes to notify when I die.
  containers::LinkedList<ProcessToNotifyOnExit,
                         &ProcessToNotifyOnExit::target_node>
      processes_to_notify_when_i_die;
  // Linked lists of processes I want to be notified of when they die.
  containers::LinkedList<ProcessToNotifyOnExit,
                         &ProcessToNotifyOnExit::notifyee_node>
      processes_i_want_to_be_notified_of_when_they_die;
  // Linked list of services I want to be notified of when they appear.
  containers::LinkedList<
      ipc::ProcessToNotifyWhenServiceAppears,
      &ipc::ProcessToNotifyWhenServiceAppears::node_in_process>
      services_i_want_to_be_notified_of_when_they_appear;
  // Linked list of services I want to be notified of when they disappear.
  containers::LinkedList<
      ipc::ProcessToNotifyWhenServiceDisappears,
      &ipc::ProcessToNotifyWhenServiceDisappears::node_in_process>
      services_i_want_to_be_notified_of_when_they_disappear;

  // Tree of services in this process.
  containers::AATree<ipc::Service, &ipc::Service::node_in_process,
                     &ipc::Service::message_id>
      services;

  // Number of services registered by this process.
  size_t service_count;

  // Tree of shared memory mapped into this process.
  containers::AATree<ipc::SharedMemoryInProcess,
                     &ipc::SharedMemoryInProcess::node_in_process,
                     &ipc::SharedMemoryInProcess::virtual_address>
      joined_shared_memories;

  // Linked list of shared memory events registered by this process.
  containers::LinkedList<ipc::SharedMemoryEvent,
                         &ipc::SharedMemoryEvent::node_in_process>
      shared_memory_events;

  // Linked list of timer events that are scheduled for this process.
  containers::LinkedList<TimerEvent, &TimerEvent::node_in_process> timer_events;

  // Whether this process has enabled profiling. This is actually a count
  // because the calls to enable profiling are nested.
  size_t has_enabled_profiling;

  // The number of CPU cycles spent executing this process while it has been
  // profiled.
  size_t cycles_spent_executing_while_profiled;

  // The timestamp (in microseconds since boot) when this process was created.
  size_t creation_timestamp;

  // The CPU time (in microseconds) spent by this process in the current epoch
  // per core.
  size_t cpu_time_in_current_epoch[kMaxCores];

  // The rolling CPU percentage byte representation (0 to 255) per core.
  uint8 rolling_cpu_percentage[kMaxCores];

  // The epoch index when the rolling CPU percentage was last caught up/updated.
  size_t last_updated_epoch;

  // Is this process currently tracked on the active list for the current epoch?
  bool is_on_active_list_this_epoch;

  // Is this process currently subscribing to CPU tracking?
  bool tracking_cpu_usage;

  // Node for the active processes list this epoch.
  containers::LinkedListNode node_active_this_epoch;

  // Node for the CPU tracking subscriptions list.
  containers::LinkedListNode node_cpu_tracking_subscription;

  // Number of RPCs this process is waiting on.
  size_t rpc_count;

  // RPCs that this process is waiting on for replies to.
  containers::LinkedList<ipc::RPC, &ipc::RPC::node_in_caller>
      rpcs_this_process_is_waiting_on;

  // RPCs that another process is waiting on this process to reply to.
  containers::AATree<ipc::RPC, &ipc::RPC::node_in_callee,
                     &ipc::RPC::synthetic_response_message_id>
      rpcs_waiting_on_this_process;

  size_t next_synthetic_rpc_response_message_id;

  // Message ID to send to the process to ask it to call 'futex wait'.
  size_t futex_wake_message_id;
};

// Initializes the internal structures for tracking processes.
void InitializeProcesses();

// Creates a process, returns kError if there was an error.
Process* CreateProcess(bool is_driver, bool can_create_processes);

// Destroys a process - DO NOT CALL THIS DIRECTLY, destroy a process by
// destroying all of its threads!
void DestroyProcess(Process* process);

// Emits binary trace event for process creation on Channel 2.
void EmitProcessCreatedTrace(Process* process);

// Emits binary trace event for process termination on Channel 2.
void EmitProcessTerminatedTrace(Process* process);

// Returns whether any processes are running.
bool AreAnyProcessesRunning();

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
// length kProcessNameLength). Returns nullptr if there are no more processes
// with the provided name. `start_from` is inclusive.
Process* FindNextProcessWithName(const char* name, Process* start_from);

// Creates a child process. The parent process must be allowed to create
// children. Returns kError if there was an error.
Process* CreateChildProcess(Process* parent, char* name, size_t bitfield);

// Unmaps memory pages from the parent and assigns them to the child. The memory
// is unmapped from the calling process regardless of if this call succeeds. If
// the page already exists in the child process, nothing is set.
void SetChildProcessMemoryPages(Process* parent, Process* child,
                                size_t source_address,
                                size_t destination_address, size_t page_count);

// Creates a thread in a process that is currently in the `creating` state.
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

// Returns whether a process is a child of a parent. Also returns false if the
// child is nullptr.
bool IsProcessAChildOfParent(Process* parent, Process* child);

// Sends a message to a process to ask it to wake its futex.
void AwakeFutexInProcess(Process* process, size_t address);

}  // namespace scheduling
