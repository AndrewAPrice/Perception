#ifndef TEST
#include "scheduler.h"

#include "interrupts.h"
#include "liballoc.h"
#include "linked_list.h"
#include "process.h"
#include "registers.h"
#include "text_terminal.h"
#include "thread.h"
#include "timer.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

// The currently executing thread. This can be nullptr if all threads are
// asleep.
Thread* running_thread;

// Currently executing registers.
Registers* currently_executing_thread_regs;

namespace {

// Linked list of awake threads we can cycle through.
LinkedList<Thread, &Thread::node_in_scheduler> awake_threads;

// The idle registers to return to when no thread is awake. (This points to
// the while(true) {hlt} in kmain.)
Registers* idle_regs;

}  // namespace

void InitializeScheduler() {
  new (&awake_threads) LinkedList<Thread, &Thread::node_in_scheduler>();
  running_thread = nullptr;
  currently_executing_thread_regs = (Registers*)malloc(sizeof(Registers));
  if (!currently_executing_thread_regs) {
    print << "Could not allocate object to store the kernel's registers.";
    __asm__ __volatile__("cli");
    __asm__ __volatile__("hlt");
  }
  idle_regs = currently_executing_thread_regs;
}

// Initializes the scheduler.
void ScheduleNextThread() {
  // The next thread to switch to.
  Thread* next;

  UpdateRunningThreadTimeslice();

  if (running_thread) {
    // A thread was currently executing.
    if (running_thread->uses_fpu_registers) {
      asm volatile("fxsave %0" ::"m"(*running_thread->fpu_registers));
    }
    // Move to the next awake thread.
    next = awake_threads.NextItem(running_thread);
    if (!next) {
      // The end of the line was reached. Switch back to the first awake thread.
      next = awake_threads.FirstItem();
    }
  } else {
    // The kernel's idle thread was active. Attempt to switch to the first
    // awake thread.
    next = awake_threads.FirstItem();
  }

  if (!next) {
    // If there's no next thread, return to the kernel's idle thread.
    running_thread = 0;
    currently_executing_thread_regs = idle_regs;
    KernelAddressSpace().SwitchToAddressSpace();
    return;
  }

  /* enter the next thread */
  running_thread = next;
  running_thread->time_slices++;
  if (running_thread->remaining_timeslice_microseconds == 0) {
    running_thread->remaining_timeslice_microseconds = 10000;
  }
  running_thread->current_run_start_timestamp =
      GetCurrentTimestampInMicroseconds();

  running_thread->process->virtual_address_space.SwitchToAddressSpace();

  if (running_thread->uses_fpu_registers) {
    asm volatile("fxrstor %0" ::"m"(*running_thread->fpu_registers));
  }
  LoadThreadSegment(running_thread);

  currently_executing_thread_regs = &running_thread->registers;
}

void ScheduleThread(Thread* thread) {
  if (thread->awake) return;
  thread->awake = true;
  awake_threads.AddBack(thread);

  ReprogramTimerForNextDeadline();
}

void UnscheduleThread(Thread* thread) {
  if (!thread->awake) return;

  UpdateRunningThreadTimeslice();

  // Attempt to schedule the next thread first. It's important to do this before
  // the awake thread is unscheduled otherwise it would jump back to the first
  // scheduled thread, which would give the earlier scheduled threads greater
  // priority.
  if (thread == running_thread) ScheduleNextThread();
  awake_threads.Remove(thread);
  thread->awake = false;
  // It is possible that there were no other threads and this thread was
  // re-scheduled. If so, attempt to reschedule another thread.
  if (thread == running_thread) ScheduleNextThread();

  ReprogramTimerForNextDeadline();
}

bool HasAtLeast2AwakeThreads() {
  Thread* first = awake_threads.FirstItem();
  if (first == nullptr) return false;
  return awake_threads.NextItem(first) != nullptr;
}

// Schedules a thread if currently halted - such as an interrupt
// woke up a thread.
void ScheduleThreadIfWeAreHalted() {
  if (running_thread == nullptr && !awake_threads.IsEmpty()) {
    // No thread was running, but there is a thread waiting to run.
    ScheduleNextThread();
  }
}

#endif  // TEST
