#include "scheduler.h"

#include "interrupts.h"
#include "liballoc.h"
#include "process.h"
#include "registers.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_allocator.h"

// Linked list of awake threads we can cycle through.
Thread *first_awake_thread;
Thread *last_awake_thread;

// The currently executing thread. This can be nullptr if all threads are asleep.
Thread *running_thread;

// The idle registers to return to when no thread is awake. (This points us to
// the while(true) {hlt} in kmain.)
Registers *idle_regs;

// Currently executing registers.
Registers *currently_executing_thread_regs;

void InitializeScheduler() {
  first_awake_thread = nullptr;
  last_awake_thread = nullptr;
  running_thread = nullptr;
  currently_executing_thread_regs = (Registers*)malloc(sizeof(Registers));
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
    next = running_thread->next_awake;
    if (!next) {
      // We reached the end of the line. Switch back to the first awake thread.
      next = first_awake_thread;
    }

  } else {
    // We were in the kernel's idle thread. Attempt to switch to the first awake
    // thread.
    next = first_awake_thread;
  }

  if (!next) {
    // If there's no next thread, we'll return to the kernel's idle thread.
    running_thread = 0;
    currently_executing_thread_regs = idle_regs;
    SwitchToAddressSpace(&kernel_address_space);
    return;
  }

  /* enter the next thread */
  running_thread = next;
  running_thread->time_slices++;

  SwitchToAddressSpace(&running_thread->process->virtual_address_space);

  if (running_thread->uses_fpu_registers) {
#ifndef __TEST__
    asm volatile("fxrstor %0" ::"m"(*running_thread->fpu_registers));
#endif
  }
  LoadThreadSegment(running_thread);

  currently_executing_thread_regs = running_thread->registers;
}

void ScheduleThread(Thread *thread) {
  // lock_interrupts();
  if (thread->awake) {
    //	unlock_interrupts();
    return;
  }

  thread->awake = true;

  thread->next_awake = 0;
  thread->previous_awake = last_awake_thread;

  if (last_awake_thread) {
    last_awake_thread->next_awake = thread;
    last_awake_thread = thread;
  } else {
    first_awake_thread = thread;
    last_awake_thread = thread;
  }
  // unlock_interrupts();
}

void UnscheduleThread(Thread *thread) {
  if (!thread->awake) {
    return;
  }

  thread->awake = false;

  if (thread->next_awake) {
    thread->next_awake->previous_awake = thread->previous_awake;
  } else {
    last_awake_thread = thread->previous_awake;
  }

  if (thread->previous_awake) {
    thread->previous_awake->next_awake = thread->next_awake;
  } else {
    first_awake_thread = thread->next_awake;
  }

  if (thread == running_thread) {
    ScheduleNextThread();
  }
}

// Schedules a thread if we are currently halted - such as an interrupt
// woke up a thread.
void ScheduleThreadIfWeAreHalted() {
  if (running_thread == nullptr && first_awake_thread != nullptr) {
    // No thread was running, but there is a thread waiting to run.
    ScheduleNextThread();
  }
}
