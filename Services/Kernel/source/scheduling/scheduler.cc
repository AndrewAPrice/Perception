#ifndef TEST
#include "scheduling/scheduler.h"

#include "../../../Libraries/perception/public/perception/tracing.h"
#include "containers/linked_list.h"
#include "containers/spinlock.h"
#include "hardware/fpu.h"
#include "hardware/io.h"
#include "hardware/registers.h"
#include "hardware/smp.h"
#include "interrupts/interrupts.h"
#include "memory/heap_allocator.h"
#include "memory/memory.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#include "output/text_terminal.h"
#include "processes/process.h"
#include "scheduling/thread.h"
#include "scheduling/timer.h"

namespace scheduling {

using containers::InterruptSafeSpinlockGuard;
using containers::LinkedList;
using hardware::ReadTimestampCounter;
using hardware::RestoreFpuState;
using hardware::SaveFpuState;
using hardware::SendRescheduleIpi;
using hardware::SendRescheduleIpiToAnyIdleCore;
using output::print;
using processes::Process;

namespace {

// Base weights allocated to proportional ready queues (Queues 2 to 4).
constexpr int kBaseCredits[kThreadPriorityCount] = {
    0,   // InterruptDriver (N/A)
    0,   // RealtimeService (N/A)
    12,  // InteractiveApp (60% of proportional share)
    5,   // Normal (25% of proportional share)
    2,   // Background (10% of proportional share)
    0    // Idle (N/A - strictly idle)
};

// First priority index with proportional scheduling credits.
constexpr int kProportionalPriorityStart =
    static_cast<int>(ThreadPriority::InteractiveApp);

// Last priority index with proportional scheduling credits.
constexpr int kProportionalPriorityEnd =
    static_cast<int>(ThreadPriority::Background);

// Idle priority queue index.
constexpr int kIdlePriorityIndex = static_cast<int>(ThreadPriority::Idle);

// Global singleton instance of Scheduler.
Scheduler g_scheduler;

#ifdef ENABLE_TRACING
void EmitContextSwitchTrace(Thread* prev, Thread* next) {
  uint64 tsc = ReadTimestampCounter();
  uint32 prev_pid =
      (prev && prev->process) ? static_cast<uint32>(prev->process->pid) : 0;
  uint32 prev_tid = prev ? static_cast<uint32>(prev->id) : 0;
  uint32 next_pid =
      (next && next->process) ? static_cast<uint32>(next->process->pid) : 0;
  uint32 next_tid = next ? static_cast<uint32>(next->id) : 0;
  uint8 reason = (prev && prev->is_awake()) ? 0 : 1;

  char packet[26];
  packet[0] = 0x05;  // CONTEXT_SWITCH opcode
  memcpy(&packet[1], (const char*)&tsc, 8);
  memcpy(&packet[9], (const char*)&prev_pid, 4);
  memcpy(&packet[13], (const char*)&prev_tid, 4);
  memcpy(&packet[17], (const char*)&next_pid, 4);
  memcpy(&packet[21], (const char*)&next_tid, 4);
  packet[25] = static_cast<char>(reason);

  ScopedPrintSource source(0, "Kernel", 2);
  for (size_t i = 0; i < 26; i++) print << packet[i];
}
#endif  // ENABLE_TRACING

}  // namespace

Scheduler& Scheduler::Get() {
  return g_scheduler;
}

Scheduler::Scheduler()
    : focused_process_(nullptr), awake_thread_count_(0) {
  for (int i = 0; i < kThreadPriorityCount; i++) {
    queue_credits_[i] = kBaseCredits[i];
  }
}

void Scheduler::Initialize() {
  for (int i = 0; i < kThreadPriorityCount; i++) {
    new (&ready_queues_[i]) LinkedList<Thread, &Thread::node_in_scheduler>();
    queue_credits_[i] = kBaseCredits[i];
  }
  awake_thread_count_ = 0;
  CurrentlyExecutingThreadRegs() = &GetCurrentCpuCore().idle_regs;
}

Thread* Scheduler::PickNextThread() {
  // If there are any drivers or realtime services to run, pick the topmost.
  for (int i = 0; i < kProportionalPriorityStart; i++) {
    if (!ready_queues_[i].IsEmpty()) return ready_queues_[i].PopFront();
  }

  // Find the highest priority queue with ready threads and remaining credits.
  for (int i = kProportionalPriorityStart; i <= kProportionalPriorityEnd; i++) {
    if (!ready_queues_[i].IsEmpty() && queue_credits_[i] > 0) {
      queue_credits_[i]--;
      return ready_queues_[i].PopFront();
    }
  }

  // If there are no ready threads in the proportional queues with credits,
  // reset credits.
  bool proportional_has_ready_threads = false;
  for (int i = kProportionalPriorityStart; i <= kProportionalPriorityEnd; i++) {
    if (!ready_queues_[i].IsEmpty()) {
      proportional_has_ready_threads = true;
      break;
    }
  }

  if (proportional_has_ready_threads) {
    for (int i = kProportionalPriorityStart; i <= kProportionalPriorityEnd; i++) {
      queue_credits_[i] = kBaseCredits[i];
    }
    for (int i = kProportionalPriorityStart; i <= kProportionalPriorityEnd; i++) {
      if (!ready_queues_[i].IsEmpty() && queue_credits_[i] > 0) {
        queue_credits_[i]--;
        return ready_queues_[i].PopFront();
      }
    }
  }

  // Run an idle thread if no other threads in any of the other priorities are awake.
  if (!ready_queues_[kIdlePriorityIndex].IsEmpty()) {
    return ready_queues_[kIdlePriorityIndex].PopFront();
  }

  return nullptr;
}

void Scheduler::ScheduleNextThread() {
  UpdateRunningThreadTimeslice();

  Thread* prev = RunningThread();
  Thread* next = nullptr;
  size_t core_id = GetCurrentCoreId();

  {
    InterruptSafeSpinlockGuard guard(lock_);

    // Fast path: if the previous thread is still awake and no other threads are
    // ready in the queues, continue running without saving/restoring FPU or
    // switching state.
    if (prev != nullptr && prev->is_awake()) {
      bool other_ready = false;
      for (int i = 0; i < kThreadPriorityCount; i++) {
        if (!ready_queues_[i].IsEmpty()) {
          other_ready = true;
          break;
        }
      }
      if (!other_ready) {
        prev->TransitionToRunning(static_cast<int>(core_id));
        prev->time_slices++;
        if (prev->remaining_timeslice_microseconds == 0)
          prev->remaining_timeslice_microseconds = kDefaultTimesliceMicroseconds;
        prev->current_run_start_timestamp = GetCurrentTimestampInMicroseconds();
        return;
      }
    }

    // Save previous thread's FPU state before making it available to other cores.
    if (prev != nullptr && prev->uses_fpu_registers) {
      SaveFpuState(prev->fpu_registers);
    }

    if (prev != nullptr) {
      prev->in_syscall = false;
      if (prev->is_awake()) {
        prev->TransitionToReady();
        int p = static_cast<int>(prev->priority);
        ready_queues_[p].AddBack(prev);
        SendRescheduleIpiToAnyIdleCore();
      }
    }

    // Pick next thread.
    next = PickNextThread();
    if (next != nullptr) {
      next->TransitionToRunning(static_cast<int>(core_id));
      __atomic_fetch_and(&g_idle_cores_mask, ~(1ULL << core_id),
                         __ATOMIC_SEQ_CST);
    } else {
      __atomic_fetch_or(&g_idle_cores_mask, 1ULL << core_id, __ATOMIC_SEQ_CST);
    }
  }

  if (!next) {
#ifdef ENABLE_TRACING
    if (prev != nullptr) EmitContextSwitchTrace(prev, nullptr);
#endif
    RunningThread() = nullptr;
    // Move off the last process's page tables. Otherwise this core keeps that
    // process's PML4 in CR3 while idling, and the address space teardown that
    // runs when the process exits would free page tables this core is still
    // translating kernel addresses through.
    memory::KernelAddressSpace().SwitchToAddressSpace();
    CurrentlyExecutingThreadRegs() = &GetCurrentCpuCore().idle_regs;
    if (prev != nullptr) {
      __atomic_store_n(&prev->running_on_core, -1, __ATOMIC_RELEASE);
    }
    return;
  }

#ifdef ENABLE_TRACING
  if (prev != next) EmitContextSwitchTrace(prev, next);
#endif

  RunningThread() = next;
  RunningThread()->time_slices++;
  if (RunningThread()->remaining_timeslice_microseconds == 0)
    RunningThread()->remaining_timeslice_microseconds =
        kDefaultTimesliceMicroseconds;
  RunningThread()->current_run_start_timestamp =
      GetCurrentTimestampInMicroseconds();

  RunningThread()->process->virtual_address_space.SwitchToAddressSpace();

  if (RunningThread()->uses_fpu_registers)
    RestoreFpuState(RunningThread()->fpu_registers);
  LoadThreadSegment(RunningThread());

  CurrentlyExecutingThreadRegs() = &RunningThread()->registers;
  if (prev != nullptr && prev != next) {
    __atomic_store_n(&prev->running_on_core, -1, __ATOMIC_RELEASE);
  }
}

void Scheduler::Schedule(Thread* thread, bool force_preemption) {
  bool should_preempt = false;
  {
    InterruptSafeSpinlockGuard guard(lock_);
    // Early return: if thread is already awake or terminated, ready queue is unchanged.
    if (thread->is_awake() || thread->is_terminated()) return;
    thread->TransitionToReady();

    awake_thread_count_++;

    int p = static_cast<int>(thread->priority);
    if (force_preemption) {
      ready_queues_[p].AddFront(thread);
      if (p >= kProportionalPriorityStart && p <= kProportionalPriorityEnd &&
          queue_credits_[p] <= 0) {
        queue_credits_[p] = 1;
      }
      should_preempt = true;
    } else {
      ready_queues_[p].AddBack(thread);
      if (RunningThread() != nullptr &&
          p < static_cast<int>(RunningThread()->priority) &&
          (p < 2 || queue_credits_[p] > 0))
        should_preempt = true;
    }
  }

  // Signal any idle core that a thread has become ready.
  SendRescheduleIpiToAnyIdleCore();

  if (should_preempt) ScheduleNextThread();

  ReprogramTimerForNextDeadline();
}

void Scheduler::ScheduleDirectSwitch(Thread* thread) {
  Schedule(thread, /*force_preemption=*/true);
}

void Scheduler::Unschedule(Thread* thread, ThreadState target_state) {
  bool is_current = false;
  int other_core = -1;
  {
    InterruptSafeSpinlockGuard guard(lock_);
    if (thread == RunningThread()) is_current = true;

    if (!thread->is_awake()) {
      if (!is_current) return;
    } else {
      UpdateRunningThreadTimeslice();

      awake_thread_count_--;

      if (is_current) {
        // Handled below; RunningThread() will be switched in ScheduleNextThread.
      } else if (thread->running_on_core == -1) {
        int p = static_cast<int>(thread->priority);
        ready_queues_[p].Remove(thread);
      } else {
        other_core = thread->running_on_core;
      }
    }

    switch (target_state) {
      case ThreadState::BlockedOnMessage:
        thread->TransitionToBlockedOnMessage();
        break;
      case ThreadState::BlockedOnMemory:
        thread->TransitionToBlockedOnMemory(
            thread->thread_is_waiting_for_shared_memory);
        break;
      case ThreadState::Terminated:
        thread->TransitionToTerminated();
        break;
      case ThreadState::Halted:
      default:
        thread->TransitionToHalted();
        break;
    }
  }

  if (is_current) {
    ScheduleNextThread();
  } else if (other_core >= 0) {
    SendRescheduleIpi(static_cast<size_t>(other_core));
  }

  ReprogramTimerForNextDeadline();
}

void Scheduler::SetPriority(Thread* thread, ThreadPriority priority_input) {
  ThreadPriority target_priority = priority_input;
  if (focused_process_ && thread->process == focused_process_ &&
      priority_input == ThreadPriority::Normal) {
    target_priority = ThreadPriority::InteractiveApp;
  }

  if (thread->priority == target_priority) return;

  InterruptSafeSpinlockGuard guard(lock_);
  if (thread->is_ready()) {
    int p = static_cast<int>(thread->priority);
    ready_queues_[p].Remove(thread);
    thread->priority = target_priority;
    ready_queues_[static_cast<int>(target_priority)].AddBack(thread);
  } else {
    thread->priority = target_priority;
  }
}

void Scheduler::SetFocusedProcess(Process* process) {
  {
    InterruptSafeSpinlockGuard guard(lock_);
    if (focused_process_ == process) return;

    // Revert old focused threads back to Normal under focused_process_->lock.
    if (focused_process_ != nullptr) {
      InterruptSafeSpinlockGuard proc_guard(focused_process_->lock);
      for (Thread* t : focused_process_->threads) {
        if (t->priority == ThreadPriority::InteractiveApp) {
          if (t->is_ready()) {
            ready_queues_[static_cast<int>(t->priority)].Remove(t);
            t->priority = ThreadPriority::Normal;
            ready_queues_[static_cast<int>(t->priority)].AddBack(t);
          } else {
            t->priority = ThreadPriority::Normal;
          }
        }
      }
    }

    focused_process_ = process;

    // Elevate new focused threads with Normal priority to InteractiveApp under process->lock.
    if (focused_process_ != nullptr) {
      InterruptSafeSpinlockGuard proc_guard(focused_process_->lock);
      for (Thread* t : focused_process_->threads) {
        if (t->priority == ThreadPriority::Normal) {
          if (t->is_ready()) {
            ready_queues_[static_cast<int>(t->priority)].Remove(t);
            t->priority = ThreadPriority::InteractiveApp;
            ready_queues_[static_cast<int>(t->priority)].AddBack(t);
          } else {
            t->priority = ThreadPriority::InteractiveApp;
          }
        }
      }
    }
  }

  ScheduleNextThread();
}

Process* Scheduler::GetFocusedProcess() {
  InterruptSafeSpinlockGuard guard(lock_);
  return focused_process_;
}

bool Scheduler::HasAwakeThreads() {
  InterruptSafeSpinlockGuard guard(lock_);
  return awake_thread_count_ > 0;
}

bool Scheduler::NeedsTimesliceInterrupt(Thread* thread) {
  if (thread == nullptr) return false;

  InterruptSafeSpinlockGuard guard(lock_);
  for (int i = 0; i < kThreadPriorityCount; i++) {
    if (!ready_queues_[i].IsEmpty()) return true;
  }
  return false;
}

void Scheduler::ScheduleIfHalted() {
  if (RunningThread() == nullptr) ScheduleNextThread();
}

// Forwarding free functions for backward compatibility.
void InitializeScheduler() {
  Scheduler::Get().Initialize();
}

void ScheduleNextThread() {
  Scheduler::Get().ScheduleNextThread();
}

void ScheduleThread(Thread* thread, bool force_preemption) {
  Scheduler::Get().Schedule(thread, force_preemption);
}

void ScheduleThreadDirectSwitch(Thread* thread) {
  Scheduler::Get().ScheduleDirectSwitch(thread);
}

void UnscheduleThread(Thread* thread, ThreadState target_state) {
  Scheduler::Get().Unschedule(thread, target_state);
}

void SetThreadPriority(Thread* thread, ThreadPriority priority) {
  Scheduler::Get().SetPriority(thread, priority);
}

void SetFocusedProcess(Process* process) {
  Scheduler::Get().SetFocusedProcess(process);
}

Process* GetFocusedProcess() {
  return Scheduler::Get().GetFocusedProcess();
}

bool HasAwakeThreads() {
  return Scheduler::Get().HasAwakeThreads();
}

bool NeedsTimesliceInterrupt(Thread* thread) {
  return Scheduler::Get().NeedsTimesliceInterrupt(thread);
}

void ScheduleThreadIfWeAreHalted() {
  Scheduler::Get().ScheduleIfHalted();
}

}  // namespace scheduling

#endif  // TEST
