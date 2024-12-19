#pragma once

#include "linked_list.h"
#include "registers.h"
#include "types.h"

struct Process;
struct ThreadSleepingForSharedMemoryPage;
struct ThreadWaitingForSharedMemoryPage;

// Represents a thread. A sequence of execution (that's part of a user process)
// that may run in parallel with other threads.
struct Thread {
  // The ID of the tread. Used it identify this thread inside the process.
  size_t id;

  // The process this thread belongs to.
  Process *process;

  // The current state of the registers. Unless this thread is actually running,
  // in which case the registers are actually in the CPU registers until the
  // next interrupt or syscall.
  Registers registers;

  // Storage for the FPU registers. Must be 16 byte aligned (our malloc
  // implementation will give us a 16 byte aligned Thread struct.) For
  // performance reasons, we only save this if uses_fpu_registers is set.
  char fpu_registers[512] __attribute__((aligned(16)));

  // Does this thread use FPU registers that we need to save them on context
  // switching?
  bool uses_fpu_registers : 1;

  // Offset of the thread's segment (FS).
  size_t thread_segment_offset;

  // Virtual address of the thread's stack. This gets released when the thread
  // is destroyed.
  size_t stack;

  // A linked list of threads in the process.
  LinkedListNode node_in_process;

  // Is this thread awake?
  bool awake : 1;

  // A linked list of awake threads, used by the scheduler.
  LinkedListNode node_in_scheduler;

  // The number of time slices this thread has ran for. This might not be so
  // accurate as to how much processing time a thread has had because partial
  // slices (such as the previous thread 'yielding') is considered a full slice
  // here.
  size_t time_slices;

  // The linked queue of threads in the process that are waiting for messages.
  LinkedListNode node_sleeping_for_messages;
  bool thread_is_waiting_for_message : 1;

  // Set if this thread is waiting for shared memory.
  ThreadWaitingForSharedMemoryPage *thread_is_waiting_for_shared_memory;

  // If not 0, the virtual address in the process's space to clear on
  // termination of the thread. Must be 8-byte aligned.
  size_t address_to_clear_on_termination;
};

// Initialize threads.
void InitializeThreads();

// Creates a thread for a process.
Thread *CreateThread(Process *process, size_t entry_point, size_t param);

// Destroys a thread.
void DestroyThread(Thread *thread, bool process_being_destroyed);

// Destroys all threads for a process.
void DestroyThreadsForProcess(Process *process, bool process_being_destroyed);

// Returns a thread with the provided tid in process, return 0 if it doesn't
// exist.
Thread *GetThreadFromTid(Process *process, size_t tid);

// Set the thread's segment offset (FS).
void SetThreadSegment(Thread *thread, size_t address);

// Load's a thread segment.
void LoadThreadSegment(Thread *thread);
