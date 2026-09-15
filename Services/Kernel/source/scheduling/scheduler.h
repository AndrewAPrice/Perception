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

#include "types.h"

#include "containers/linked_list.h"
#include "containers/spinlock.h"
#include "hardware/registers.h"
#include "scheduling/cpu_core.h"
#include "scheduling/thread.h"
#include "scheduling/thread_state.h"

namespace processes {
struct Process;
}

namespace scheduling {

struct Thread;

// Returns a reference to the currently running thread on the local CPU core.
inline Thread*& RunningThread() {
  return GetCurrentCpuCore().running_thread;
}

// Returns a reference to the currently executing registers on the local CPU core.
inline hardware::Registers*& CurrentlyExecutingThreadRegs() {
  return GetCurrentCpuCore().currently_executing_thread_regs;
}

// Manages thread scheduling, priority ready queues, and CPU time allocation.
class Scheduler {
 public:
  // Returns the singleton instance of Scheduler.
  static Scheduler& Get();

  // Constructs the Scheduler.
  Scheduler();

  // Initializes scheduler queues and state.
  void Initialize();

  // Schedules the next thread, called from timer interrupt or yield.
  void ScheduleNextThread();

  // Schedules a thread. If force_preemption is true, preempts current execution.
  void Schedule(Thread* thread, bool force_preemption = false);

  // Directly switches to a thread on the local CPU core.
  void ScheduleDirectSwitch(Thread* thread);

  // Unschedules a thread and transitions it to target_state.
  void Unschedule(Thread* thread,
                  ThreadState target_state = ThreadState::Halted);

  // Sets a thread's base priority and reschedules it if awake.
  void SetPriority(Thread* thread, ThreadPriority priority);

  // Sets the currently focused process for priority elevation.
  void SetFocusedProcess(processes::Process* process);

  // Returns the currently focused process.
  processes::Process* GetFocusedProcess();

  // Returns whether any threads are currently awake.
  bool HasAwakeThreads();

  // Returns whether the running thread needs a timeslice interrupt.
  bool NeedsTimesliceInterrupt(Thread* thread);

  // Schedules a thread if the local core is currently halted.
  void ScheduleIfHalted();

  // Returns spinlock synchronizing scheduler operations.
  containers::InterruptSafeSpinlock& lock() { return lock_; }

 private:
  // Pops the next eligible thread from ready queues according to priority and credits.
  Thread* PickNextThread();

  containers::InterruptSafeSpinlock lock_;
  containers::LinkedList<Thread, &Thread::node_in_scheduler>
      ready_queues_[kThreadPriorityCount];
  int queue_credits_[kThreadPriorityCount];
  processes::Process* focused_process_;
  int awake_thread_count_;
};

// Initializes the scheduler.
void InitializeScheduler();

// Schedule the next thread, called from the timer interrupt.
void ScheduleNextThread();

// Schedules a thread. If force_preemption is true, the current thread on the
// local CPU core will immediately yield to the scheduled thread.
void ScheduleThread(Thread *thread, bool force_preemption = false);

// Directly switches to a thread on the local CPU core.
void ScheduleThreadDirectSwitch(Thread* thread);

// Unschedules a thread and transitions it to the specified target state.
void UnscheduleThread(Thread *thread,
                      ThreadState target_state = ThreadState::Halted);

// Set a thread's base priority and reschedule it if awake.
void SetThreadPriority(Thread* thread, ThreadPriority priority);

// Focused process elevation interface for Window Manager.
void SetFocusedProcess(processes::Process* process);
processes::Process* GetFocusedProcess();
bool HasAwakeThreads();

// Returns whether the running thread needs a timeslice interrupt.
bool NeedsTimesliceInterrupt(Thread* thread);

// Schedules a thread if currently halted - such as an interrupt
// woke up a thread.
void ScheduleThreadIfWeAreHalted();

}  // namespace scheduling