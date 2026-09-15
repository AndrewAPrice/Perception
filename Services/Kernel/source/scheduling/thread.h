#pragma once

#include "containers/aa_tree.h"
#include "containers/linked_list.h"
#include "hardware/fpu.h"
#include "hardware/registers.h"
#include "scheduling/thread_state.h"
#include "types.h"

namespace ipc {
struct ThreadWaitingForSharedMemoryPage;
}

namespace processes {
struct Process;
}

namespace scheduling {

// Default thread timeslice duration in microseconds (10 ms).
constexpr size_t kDefaultTimesliceMicroseconds = 10000;

// Represents a thread. A sequence of execution (that's part of a user process)
// that may run in parallel with other threads.
struct Thread {
  // Checks if the thread is currently eligible to run or actively running.
  bool is_awake() const {
    return state == ThreadState::Ready || state == ThreadState::Running;
  }

  // Checks if the thread is queued in the scheduler ready queues.
  bool is_ready() const { return state == ThreadState::Ready; }

  // Checks if the thread is actively executing on a CPU core.
  bool is_running() const { return state == ThreadState::Running; }

  // Checks if the thread is sleeping waiting for an incoming message.
  bool is_waiting_for_message() const {
    return state == ThreadState::BlockedOnMessage;
  }

  // Checks if the thread is sleeping waiting for a shared memory page.
  bool is_waiting_for_shared_memory() const {
    return state == ThreadState::BlockedOnMemory;
  }

  // Checks if the thread is halted.
  bool is_halted() const { return state == ThreadState::Halted; }

  // Checks if the thread is marked as terminated.
  bool is_terminated() const { return state == ThreadState::Terminated; }

  // Transitions the thread to the Ready state.
  void TransitionToReady();

  // Transitions the thread to the Running state on the specified core.
  void TransitionToRunning(int core_id);

  // Transitions the thread to the BlockedOnMessage state.
  void TransitionToBlockedOnMessage();

  // Transitions the thread to the BlockedOnMemory state.
  void TransitionToBlockedOnMemory(
      ipc::ThreadWaitingForSharedMemoryPage* wait_record);

  // Transitions the thread to the Halted state.
  void TransitionToHalted();

  // Transitions the thread to the Terminated state.
  void TransitionToTerminated();

  // Set the thread's segment offset (FS).
  void SetSegment(size_t address);

  // Set the thread's segments (FS and/or GS).
  void SetSegments(size_t fs_address, bool set_fs, size_t gs_address,
                   bool set_gs);

  // Loads a thread segment into CPU MSRs.
  void LoadSegment();

  // The ID of the thread. Used to identify this thread inside the process.
  size_t id;

  // The process this thread belongs to.
  processes::Process* process;

  // The current state of the registers. Unless this thread is actually running,
  // in which case the registers are actually in the CPU registers until the
  // next interrupt or syscall.
  hardware::Registers registers;

  // Storage for the FPU registers (allocated from FPU pool, 64-byte aligned).
  // For performance reasons, this is only set if uses_fpu_registers is true.
  hardware::FpuRegisters* fpu_registers;

  // Does this thread use FPU registers that need to be saved on context
  // switching?
  bool uses_fpu_registers : 1;

  // Offset of the thread's FS segment.
  size_t thread_fs_segment_offset;

  // Offset of the thread's GS segment.
  size_t thread_gs_segment_offset;

  // Whether the kernel allocated this thread's stack.
  bool stack_allocated_by_kernel : 1;

  // Virtual address of the thread's stack. This gets released when the thread
  // is destroyed.
  size_t stack;

  // AA tree node of threads in the process.
  containers::AATreeNode node_in_process;

  // Current execution state in the finite state machine.
  ThreadState state;

  // A linked list of awake threads, used by the scheduler.
  containers::LinkedListNode node_in_scheduler;

  // The dynamic priority of the thread.
  ThreadPriority priority;

  // The number of time slices this thread has run for. This might not be so
  // accurate as to how much processing time a thread has had because partial
  // slices (such as the previous thread 'yielding') is considered a full slice
  // here.
  size_t time_slices;

  // The remaining timeslice for this thread in microseconds.
  size_t remaining_timeslice_microseconds;

  // The timestamp (in microseconds since boot) when this thread started its current run.
  size_t current_run_start_timestamp;

  // The linked queue of threads in the process that are waiting for messages.
  containers::LinkedListNode node_sleeping_for_messages;

  // Set if this thread is waiting for shared memory.
  ipc::ThreadWaitingForSharedMemoryPage* thread_is_waiting_for_shared_memory;

  // If not 0, the virtual address in the process's space to clear on
  // termination of the thread. Must be 8-byte aligned.
  size_t address_to_clear_on_termination;

  // Whether a wake signal was sent to this thread while it was awake.
  bool wake_signal_pending : 1;

  // Is this thread currently executing a syscall?
  bool in_syscall;

  // CPU core this thread is currently executing on, or -1 if not running.
  int running_on_core;

  // Number of outstanding references to this thread.
  size_t reference_count;
};

// Increments the reference count of a thread.
void AcquireThreadReference(Thread& thread);

// Decrements the reference count of a thread and frees it to the pool if zero.
void ReleaseThreadReference(Thread& thread);

// Initialize threads.
void InitializeThreads();

// Creates a thread for a process.
Thread* CreateThread(processes::Process* process, size_t entry_point,
                     size_t param, size_t stack_pointer = 0,
                     size_t tls_base = 0);

// Destroys a thread.
void DestroyThread(Thread *thread, bool process_being_destroyed);


// Destroys all threads for a process.
void DestroyThreadsForProcess(processes::Process* process,
                              bool process_being_destroyed);

// Returns a thread with the provided tid in process, returns nullptr if it
// doesn't exist.
Thread* GetThreadFromTid(processes::Process* process, size_t tid);

// Set the thread's segment offset (FS).
void SetThreadSegment(Thread *thread, size_t address);

// Set the thread's segments (FS and/or GS).
void SetThreadSegments(Thread *thread, size_t fs_address, bool set_fs,
                       size_t gs_address, bool set_gs);

// Loads a thread segment.
void LoadThreadSegment(Thread *thread);

}  // namespace scheduling

#include "scheduling/scheduler.h"

