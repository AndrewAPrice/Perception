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

// Ready queues for each of the 6 priority levels.
LinkedList<Thread, &Thread::node_in_scheduler>
    ready_queues[kThreadPriorityCount];

// Epoch credits remaining for proportional ready queues (Queues 2 to 4).
int queue_credits[kThreadPriorityCount];

// Base weights allocated to proportional ready queues (Queues 2 to 4).
constexpr int kBaseCredits[kThreadPriorityCount] = {
    0,   // InterruptDriver (N/A)
    0,   // RealtimeService (N/A)
    12,  // InteractiveApp (60% of proportional share)
    5,   // Normal (25% of proportional share)
    2,   // Background (10% of proportional share)
    0    // Idle (N/A - strictly idle)
};

// The currently focused process (elevated dynamically).
Process* focused_process = nullptr;

// The number of currently awake threads in the scheduler.
int awake_thread_count = 0;

// The idle registers to return to when no thread is awake. (This points to
// the while(true) {hlt} in kmain.)
Registers* idle_regs;

// Returns the next thread to run. Returns nullptr if there is no thread.
Thread* PickNextThread() {
  // If there are any drivers then realtime services to run, pick the top most.
  for (int i = 0; i < 2; i++) {
    if (!ready_queues[i].IsEmpty()) return ready_queues[i].FirstItem();
  }

  // Find the highest priority queue with ready threads and remaining credits.
  for (int i = 2; i <= 4; i++) {
    if (!ready_queues[i].IsEmpty() && queue_credits[i] > 0) {
      Thread* next = ready_queues[i].FirstItem();
      queue_credits[i]--;
      return next;
    }
  }

  // If there are no ready threads in the proportional queues, reset the
  // credits.
  bool proportional_has_ready_threads = false;
  for (int i = 2; i <= 4; i++) {
    if (!ready_queues[i].IsEmpty()) {
      proportional_has_ready_threads = true;
      break;
    }
  }

  if (proportional_has_ready_threads) {
    // Reset credits for Queues 2 to 4
    for (int i = 2; i <= 4; i++) {
      queue_credits[i] = kBaseCredits[i];
    }
    // Try picking again after the epoch reset.
    for (int i = 2; i <= 4; i++) {
      if (!ready_queues[i].IsEmpty() && queue_credits[i] > 0) {
        Thread* next = ready_queues[i].FirstItem();
        queue_credits[i]--;
        return next;
      }
    }
  }

  // Run an idle thread if no other threads in any of the other priorities are
  // awake.
  if (!ready_queues[5].IsEmpty()) return ready_queues[5].FirstItem();

  return nullptr;
}

}  // namespace

void InitializeScheduler() {
  for (int i = 0; i < kThreadPriorityCount; i++) {
    new (&ready_queues[i]) LinkedList<Thread, &Thread::node_in_scheduler>();
    queue_credits[i] = kBaseCredits[i];
  }
  focused_process = nullptr;
  running_thread = nullptr;
  awake_thread_count = 0;
  currently_executing_thread_regs = (Registers*)malloc(sizeof(Registers));
  if (!currently_executing_thread_regs) {
    print << "Could not allocate object to store the kernel's registers.";
    __asm__ __volatile__("cli");
    __asm__ __volatile__("hlt");
  }
  idle_regs = currently_executing_thread_regs;
}

// Schedule the next thread.
void ScheduleNextThread() {
  UpdateRunningThreadTimeslice();

  if (running_thread) {
    if (running_thread->uses_fpu_registers)
      asm volatile("fxsave %0" ::"m"(*running_thread->fpu_registers));

    // Rotate the ready queue so the current thread goes to the back.
    if (running_thread->awake) {
      int p = static_cast<int>(running_thread->priority);
      ready_queues[p].Remove(running_thread);
      ready_queues[p].AddBack(running_thread);
    }
  }

  Thread* next = PickNextThread();
  if (!next) {
    // If there's no next thread, return to the kernel's idle thread.
    running_thread = 0;
    currently_executing_thread_regs = idle_regs;
    KernelAddressSpace().SwitchToAddressSpace();
    return;
  }

  // Enter the next thread.
  running_thread = next;
  running_thread->time_slices++;
  if (running_thread->remaining_timeslice_microseconds == 0) {
    running_thread->remaining_timeslice_microseconds = 10000;
  }
  running_thread->current_run_start_timestamp =
      GetCurrentTimestampInMicroseconds();

  running_thread->process->virtual_address_space.SwitchToAddressSpace();

  if (running_thread->uses_fpu_registers)
    asm volatile("fxrstor %0" ::"m"(*running_thread->fpu_registers));
  LoadThreadSegment(running_thread);

  currently_executing_thread_regs = &running_thread->registers;
}

void ScheduleThread(Thread* thread) {
  if (thread->awake) return;
  thread->awake = true;
  awake_thread_count++;

  int p = static_cast<int>(thread->priority);
  ready_queues[p].AddBack(thread);

  // Check if the current thread should be preempted.
  if (running_thread && (p < static_cast<int>(running_thread->priority))) {
    // Preempt if strict priority class or if queue has remaining credits.
    if (p < 2 || queue_credits[p] > 0) ScheduleNextThread();
  }

  ReprogramTimerForNextDeadline();
}

void UnscheduleThread(Thread* thread) {
  if (!thread->awake) return;

  UpdateRunningThreadTimeslice();

  int p = static_cast<int>(thread->priority);
  ready_queues[p].Remove(thread);
  thread->awake = false;
  awake_thread_count--;

  if (thread == running_thread) ScheduleNextThread();

  ReprogramTimerForNextDeadline();
}

void SetThreadPriority(Thread* thread, ThreadPriority priority_input) {
  ThreadPriority target_priority = priority_input;
  if (focused_process && thread->process == focused_process &&
      priority_input == ThreadPriority::Normal) {
    target_priority = ThreadPriority::InteractiveApp;
  }

  if (thread->priority == target_priority) return;

  bool was_awake = thread->awake;
  if (was_awake) {
    int p = static_cast<int>(thread->priority);
    ready_queues[p].Remove(thread);
  }

  thread->priority = target_priority;

  if (was_awake) {
    int p = static_cast<int>(thread->priority);
    ready_queues[p].AddBack(thread);
  }
}

void SetFocusedProcess(Process* process) {
  if (focused_process == process) return;

  // Revert old focused threads back to Normal.
  if (focused_process != nullptr) {
    for (Thread* t : focused_process->threads) {
      if (t->priority == ThreadPriority::InteractiveApp) {
        bool was_awake = t->awake;
        if (was_awake) {
          ready_queues[static_cast<int>(t->priority)].Remove(t);
        }
        t->priority = ThreadPriority::Normal;
        if (was_awake) {
          ready_queues[static_cast<int>(t->priority)].AddBack(t);
        }
      }
    }
  }

  focused_process = process;

  // Elevate new focused threads with Normal priority to InteractiveApp
  if (focused_process != nullptr) {
    for (Thread* t : focused_process->threads) {
      if (t->priority == ThreadPriority::Normal) {
        bool was_awake = t->awake;
        if (was_awake) {
          ready_queues[static_cast<int>(t->priority)].Remove(t);
        }
        t->priority = ThreadPriority::InteractiveApp;
        if (was_awake) {
          ready_queues[static_cast<int>(t->priority)].AddBack(t);
        }
      }
    }
  }

  ScheduleNextThread();
}

Process* GetFocusedProcess() { return focused_process; }

bool NeedsTimesliceInterrupt(Thread* thread) {
  if (thread == nullptr) return false;

  int p = static_cast<int>(thread->priority);
  if (p == 0 || p == 1 || p == 5) {
    // Drivers, Realtime services, and Idle threads only need timeslices if
    // there is another driver/realtime service to share time with.
    Thread* first = ready_queues[p].FirstItem();
    return first != nullptr && ready_queues[p].NextItem(first) != nullptr;
  } else {
    // Proportional threads (Queues 2, 3, 4) need timeslices if there are at
    // least two threads across all proportional queues combined.
    int proportional_awake_count = 0;
    for (int i = 2; i <= 4; i++) {
      for (Thread* t : ready_queues[i]) {
        proportional_awake_count++;
        if (proportional_awake_count >= 2) return true;
      }
    }
    return false;
  }
}

// Schedules a thread if the CPU is currently halted - such as an interrupt
// woke up a thread.
void ScheduleThreadIfWeAreHalted() {
  if (running_thread == nullptr) ScheduleNextThread();
}

#endif  // TEST
