#include "scheduler.h"

#include "interrupts.h"
#include "liballoc.h"
#include "linked_list.h"
#include "process.h"
#include "registers.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

// The currently executing thread. This can be nullptr if all threads are
// asleep.
Thread *running_thread;

// Currently executing registers.
Registers *currently_executing_thread_regs;

namespace {

// Linked list of awake threads we can cycle through.
LinkedList<Thread, &Thread::node_in_scheduler> awake_threads;

// The idle registers to return to when no thread is awake. (This points us to
// the while(true) {hlt} in kmain.)
Registers *idle_regs;

}  // namespace

void InitializeScheduler() {
  new (&awake_threads) LinkedList<Thread, &Thread::node_in_scheduler>();
  running_thread = nullptr;
  currently_executing_thread_regs = (Registers *)malloc(sizeof(Registers));
  if (!currently_executing_thread_regs) {
    print << "Could not allocate object to store the kernel's registers.";
#ifndef __TEST__
    __asm__ __volatile__("cli");
    __asm__ __volatile__("hlt");
#endif
  }
  idle_regs = currently_executing_thread_regs;
}

// Initializes the scheduler.
void ScheduleNextThread() {
  // The next thread to switch to.
  Thread *next;

  if (running_thread) {
    // We were currently executing a thread.
    if (running_thread->uses_fpu_registers) {
#ifndef __TEST__
      asm volatile("fxsave %0" ::"m"(*running_thread->fpu_registers));
#endif
    }

    // Move to the next awake thread.
    next = awake_threads.NextItem(running_thread);
    if (!next) {
      // We reached the end of the line. Switch back to the first awake thread.
      next = awake_threads.FirstItem();
    }

  } else {
    // We were in the kernel's idle thread. Attempt to switch to the first awake
    // thread.
    next = awake_threads.FirstItem();
  }

  if (!next) {
    // If there's no next thread, we'll return to the kernel's idle thread.
    running_thread = 0;
    currently_executing_thread_regs = idle_regs;
    KernelAddressSpace().SwitchToAddressSpace();
    return;
  }

  /* enter the next thread */
  running_thread = next;
  running_thread->time_slices++;

  running_thread->process->virtual_address_space.SwitchToAddressSpace();

  if (running_thread->uses_fpu_registers) {
#ifndef __TEST__
    asm volatile("fxrstor %0" ::"m"(*running_thread->fpu_registers));
#endif
  }
  LoadThreadSegment(running_thread);

  currently_executing_thread_regs = &running_thread->registers;
}

void ScheduleThread(Thread *thread) {
  if (thread->awake) return;
  thread->awake = true;
  awake_threads.AddBack(thread);
}

void UnscheduleThread(Thread *thread) {
  if (!thread->awake) return;
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
}

// Schedules a thread if we are currently halted - such as an interrupt
// woke up a thread.
void ScheduleThreadIfWeAreHalted() {
  if (running_thread == nullptr && !awake_threads.IsEmpty()) {
    // No thread was running, but there is a thread waiting to run.
    ScheduleNextThread();
  }
}
