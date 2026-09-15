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
#include "interrupts/interrupts.h"
#include "containers/linked_list.h"
#include "ipc/message_queue.h"
#include "ipc/messages.h"
#include "ipc/rpc.h"
#include "ipc/service.h"
#include "ipc/shared_memory.h"
#include "ipc/shared_memory_event.h"
#include "containers/spinlock.h"
#include "processes/process_threads.h"
#include "scheduling/thread.h"
#include "scheduling/timer_event.h"
#include "types.h"
#include "scheduling/cpu_core.h"
#include "memory/virtual_address_space.h"

namespace processes {

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
  // Constructs process with composed subsystems.
  Process()
      : message_queue(*this),
        message_lock(message_queue.lock()),
        queued_messages(message_queue.queued_messages()),
        messages_queued(0),
        threads_sleeping_for_message(message_queue.sleeping_threads()),
        threads(*this),
        reference_count(1),
        is_dying(false),
        is_on_active_list_this_epoch(false),
        tracking_cpu_usage(false) {}


  // Number of outstanding references to this process. The process manager holds
  // one for as long as the process is discoverable, and every lookup that hands
  // out a pointer holds one for as long as the caller might dereference it. The
  // object is freed when this reaches zero.
  size_t reference_count;

  // Set once, by whichever core wins the race to tear this process down. Makes
  // destruction idempotent and stops lookups from handing out a process that is
  // already being torn down.
  bool is_dying;

  // Spinlock protecting process metadata, threads, services, and child processes.
  containers::InterruptSafeSpinlock lock;

  // Message queue managing messages and sleeping threads for this process.
  ipc::MessageQueue message_queue;

  // Spinlock protecting message queues, sleeping threads, and RPC tracking.
  containers::InterruptSafeSpinlock& message_lock;

  // Queued messages sent to this process, waiting to be consumed.
  containers::LinkedList<ipc::Message, &ipc::Message::node>& queued_messages;

  // Number of messages queued.
  size_t messages_queued;

  // Linked queue of threads that are currently sleeping and waiting for a
  // message.
  containers::LinkedList<scheduling::Thread, &scheduling::Thread::node_sleeping_for_messages>&
      threads_sleeping_for_message;

  // Unique ID to identify this process.
  size_t pid;

  // Name of the process.
  char name[kProcessNameLength + 1];

  // Is this process a driver? Drivers have permission to do IO.
  bool is_driver;

  // Is this process allowed to create other processes?
  bool can_create_processes;

  // Is this process allowed to set the focused process?
  bool can_set_focus;

  // Is this process allowed to terminate other processes?
  bool can_terminate_processes;

  // Process ID of the parent that created this process, or 0 if created by the kernel.
  size_t parent_pid;

  // The parent of the current process. Only set if the process is in the
  // `creator` state.
  Process* parent;
  // A linked list of child processes in the `creator` state.
  Process* child_processes;
  // The next child process in a linked list in the parent.
  Process* next_child_process_in_parent;

  // The virtual address space that is unique to this process.
  memory::VirtualAddressSpace virtual_address_space;

  // Linked list of messages to fire on an interrupt.
  containers::LinkedList<interrupts::MessageToFireOnInterrupt,
                         &interrupts::MessageToFireOnInterrupt::node_in_process>
      messages_to_fire_on_interrupt;

  // Threads in this process.
  ProcessThreads threads;

  // Tree node of processes.

  containers::AATreeNode node_in_all_processes;

  // Linked lists of processes to notify when I die.
  containers::LinkedList<ProcessToNotifyOnExit, &ProcessToNotifyOnExit::target_node>
      processes_to_notify_when_i_die;
  // Linked lists of processes I want to be notified of when they die.
  containers::LinkedList<ProcessToNotifyOnExit, &ProcessToNotifyOnExit::notifyee_node>
      processes_i_want_to_be_notified_of_when_they_die;
  // Linked list of services I want to be notified of when they appear.
  containers::LinkedList<ipc::ProcessToNotifyWhenServiceAppears,
                         &ipc::ProcessToNotifyWhenServiceAppears::node_in_process>
      services_i_want_to_be_notified_of_when_they_appear;
  // Linked list of services I want to be notified of when they disappear.
  containers::LinkedList<ipc::ProcessToNotifyWhenServiceDisappears,
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
  containers::LinkedList<scheduling::TimerEvent, &scheduling::TimerEvent::node_in_process> timer_events;

  // Number of timer events currently scheduled for this process.
  size_t timer_event_count;

  // Number of time info change subscriptions registered by this process.
  size_t time_info_subscription_count;

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
  size_t cpu_time_in_current_epoch[scheduling::kMaxCores];

  // The rolling CPU percentage byte representation (0 to 255) per core.
  uint8 rolling_cpu_percentage[scheduling::kMaxCores];

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

// Adds a reference to a process, keeping it alive until the reference is
// released. The caller must already hold a reference, or the process lock.
void AcquireProcessReference(Process& process);

// Drops a reference. Frees the process once the last one is gone, which may
// happen on a different core than the one that destroyed it.
void ReleaseProcessReference(Process& process);

// Owns a reference to a Process, keeping it alive for the holder's lifetime.
// Returned by the lookup functions below, which acquire the reference while
// holding the process manager's lock so that the process cannot be freed
// between the lookup and the caller's use of it.
class ProcessRef {
 public:
  ProcessRef() : process_(nullptr) {}

  // Adopts a reference that has already been acquired on the caller's behalf.
  explicit ProcessRef(Process* process) : process_(process) {}

  ~ProcessRef() {
    if (process_ != nullptr) ReleaseProcessReference(*process_);
  }

  ProcessRef(ProcessRef&& other) : process_(other.process_) {
    other.process_ = nullptr;
  }

  ProcessRef& operator=(ProcessRef&& other) {
    if (this != &other) {
      if (process_ != nullptr) ReleaseProcessReference(*process_);
      process_ = other.process_;
      other.process_ = nullptr;
    }
    return *this;
  }

  ProcessRef(const ProcessRef& other) : process_(other.process_) {
    if (process_ != nullptr) AcquireProcessReference(*process_);
  }

  ProcessRef& operator=(const ProcessRef& other) {
    if (this != &other) {
      if (process_ != nullptr) ReleaseProcessReference(*process_);
      process_ = other.process_;
      if (process_ != nullptr) AcquireProcessReference(*process_);
    }
    return *this;
  }

  Process* get() const { return process_; }
  Process* operator->() const { return process_; }
  Process& operator*() const { return *process_; }
  explicit operator bool() const { return process_ != nullptr; }
  bool operator==(const Process* other) const { return process_ == other; }
  bool operator!=(const Process* other) const { return process_ != other; }
  bool operator==(decltype(nullptr)) const { return process_ == nullptr; }
  bool operator!=(decltype(nullptr)) const { return process_ != nullptr; }

 private:
  Process* process_;
};


// Initializes the internal structures for tracking processes.
void InitializeProcesses();

// Creates a process, returns nullptr if there was an error.
Process* CreateProcess(bool is_driver, bool can_create_processes,
                       bool can_set_focus = false,
                       bool can_terminate_processes = false,
                       const char* name = nullptr);


// Destroys a process - DO NOT CALL THIS DIRECTLY, destroy a process by
// destroying all of its threads! Safe to call more than once and from more
// than one core; only the first call performs the teardown.
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

// Returns a held reference to the process with the provided pid, or an empty
// reference if it doesn't exist or is being torn down.
ProcessRef GetProcessFromPid(size_t pid);

// Returns a held reference to the process with the provided pid, or if it
// doesn't exist, the process with the next highest pid. Returns an empty
// reference if no process exists with a pid >= pid. Iterate by passing the
// previous result's pid plus one, which re-enters the tree under the lock
// rather than holding a node pointer across the release.
ProcessRef GetProcessOrNextFromPid(size_t pid);

// Queries processes matching name starting from min_pid.
// Populates pids up to max_results under a single lock acquisition.
// Returns total number of matching processes found.
size_t QueryProcesses(const char* name, size_t min_pid, size_t* pids,
                      size_t max_results);

// Safely copies the name of a process into name_out under lock.
// Returns true if the process was found.
bool GetProcessName(size_t pid, char* name_out);

// Returns a held reference to the next process at or above `min_pid` with the
// given name (which must be an array of length kProcessNameLength). Returns an
// empty reference if there are no more processes with the provided name.
ProcessRef FindNextProcessWithName(const char* name, size_t min_pid);

// Creates a child process. The parent process must be allowed to create
// children. Returns nullptr if there was an error.
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

// Returns whether a process is a child of a parent. Also returns false if the
// child is nullptr.
bool IsProcessAChildOfParent(Process* parent, Process* child);

// Sends a message to a process to ask it to wake its futex.
void AwakeFutexInProcess(Process* process, size_t address);

}  // namespace processes
