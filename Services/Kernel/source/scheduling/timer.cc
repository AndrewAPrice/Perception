#include "scheduling/timer.h"

#include "hardware/acpi.h"
#include "memory/heap_allocator.h"
#include "interrupts/interrupts.h"
#include "hardware/io.h"
#include "hardware/lapic.h"
#include "containers/linked_list.h"
#include "memory/memory.h"
#include "ipc/messages.h"
#include "containers/object_pool.h"
#include "scheduling/cpu_core.h"
#include "processes/process.h"
#include "diagnostics/profiling.h"
#include "scheduling/scheduler.h"
#include "containers/spinlock.h"
#include "output/text_terminal.h"
#include "scheduling/timer_event.h"
#include "memory/virtual_allocator.h"

#ifndef TEST
namespace hardware {
volatile uint32* g_lapic_base = nullptr;
}
#endif

namespace scheduling {

using containers::AATree;
using containers::InterruptSafeSpinlock;
using containers::InterruptSafeSpinlockGuard;
using containers::LinkedList;
using containers::LinkedListNode;
using containers::ObjectPool;
#ifndef TEST
using hardware::g_lapic_base;
#endif
using hardware::GetCpuId;
using hardware::GetLocalApicPhysicalAddress;
using hardware::kApicTimerInterruptVector;
using hardware::kLapicSpuriousInterruptVectorRegister;
using hardware::kLapicTimerCurrentCountRegister;
using hardware::kLapicTimerDivideConfigurationRegister;
using hardware::kLapicTimerInitialCountRegister;
using hardware::kLapicTimerLvtRegister;
using hardware::kLapicTimerMasked;
using hardware::ReadIOByte;
using hardware::ReadLapicRegister;
using hardware::ReadTimestampCounter;
using hardware::WriteIOByte;
using hardware::WriteLapicRegister;
using memory::KernelAddressSpace;
using output::NumberFormat;
using output::print;
using processes::GetProcessOrNextFromPid;
using processes::Process;
using processes::ProcessRef;

// TSC ticks per microsecond.
uint64 g_tsc_ticks_per_microsecond = 1;

#ifndef TEST
uint64 g_lapic_ticks_per_microsecond = 1;

void SleepMicroseconds(size_t microseconds) {
  uint64 start = ReadTimestampCounter();
  uint64 target = start + (microseconds * g_tsc_ticks_per_microsecond);
  while (ReadTimestampCounter() < target) __builtin_ia32_pause();
}
#endif

namespace {

#ifdef PROFILING_ENABLED
// Interval in microseconds between profiling output dumps.
constexpr size_t kProfileIntervalInMicroseconds = 10000000;
#endif

// Spinlock protecting scheduled timer events and CPU usage tracking structures.
InterruptSafeSpinlock g_timer_spinlock;

// Spinlock protecting 'utc_offset' and 'time_info_change_subscriptions'. This
// is separate from 'g_timer_spinlock' because notifying subscribers can reach
// the scheduler, which reprograms the timer and acquires 'g_timer_spinlock'.
InterruptSafeSpinlock g_time_info_spinlock;

struct TimeInfoChangeSubscription {
  Process* process;
  size_t message_id;
  LinkedListNode node;
};

size_t utc_offset = 0;
LinkedList<TimeInfoChangeSubscription, &TimeInfoChangeSubscription::node>
    time_info_change_subscriptions;

volatile size_t microseconds_since_kernel_started;
AATree<TimerEvent, &TimerEvent::node_in_all_timer_events,
       &TimerEvent::timestamp_to_trigger_at>
    scheduled_timer_events;

#ifdef PROFILING_ENABLED
size_t microseconds_until_next_profile;
#endif

// Global list of processes that were active during the current epoch.
LinkedList<Process, &Process::node_active_this_epoch>
    active_processes_this_epoch;

// Global list of processes subscribing to CPU tracking.
LinkedList<Process, &Process::node_cpu_tracking_subscription>
    processes_subscribing_to_cpu_tracking;

// The current epoch index (increments once per second).
size_t current_epoch_count = 0;

// The timestamp of the last CPU percentage calculation epoch.
size_t last_cpu_epoch_timestamp = 0;

#ifndef TEST
uint64 boot_tsc_value = 0;
bool has_invariant_tsc = false;

void CalibrateTsc() {
  // Detect Invariant TSC support
  uint32 eax, ebx, ecx, edx;
  GetCpuId(0x80000007, eax, ebx, ecx, edx);
  has_invariant_tsc = (edx & (1 << 8)) != 0;

  if (!has_invariant_tsc)
    print << "Warning: Invariant TSC not supported on this CPU!\n";

  // Calibrate against PIT Channel 2 over 10ms
  uint8 val = ReadIOByte(0x61);
  WriteIOByte(0x61, val & 0xFD);  // Disable speaker, gate clear

  // Program PIT Channel 2: Mode 0, LOBYTE/HIBYTE, Binary
  WriteIOByte(0x43, 0b10110000);

  // Count for 10ms (11932 ticks)
  uint16 count = 11932;
  WriteIOByte(0x42, count & 0xFF);
  WriteIOByte(0x42, count >> 8);

  // Start timer: set gate high
  val = ReadIOByte(0x61);
  WriteIOByte(0x61, (val & 0xFD) | 1);

  uint64 tsc_start = ReadTimestampCounter();

  // Wait for PIT to finish counting
  while ((ReadIOByte(0x61) & 0x20) == 0) {
    // Busy loop
  }

  uint64 tsc_end = ReadTimestampCounter();

  // Disable PIT Channel 2 gate
  val = ReadIOByte(0x61);
  WriteIOByte(0x61, val & 0xFE);

  boot_tsc_value = tsc_start;
  uint64 elapsed_tsc = tsc_end - tsc_start;
  g_tsc_ticks_per_microsecond = elapsed_tsc / 10000;

  if (g_tsc_ticks_per_microsecond == 0) g_tsc_ticks_per_microsecond = 1;

  print << "TSC Calibrated: " << g_tsc_ticks_per_microsecond
        << " ticks/microsecond ("
        << (g_tsc_ticks_per_microsecond * 1000000) / 1000000000 << "."
        << ((g_tsc_ticks_per_microsecond * 1000000) % 1000000000) / 1000000
        << " GHz)\n";
}
#endif

#ifndef TEST

void InitializeLapic() {
  // Map the LAPIC base address discovered via ACPI MADT.
  size_t lapic_phys = GetLocalApicPhysicalAddress();
  size_t virtual_addr = KernelAddressSpace().MapPhysicalPages(lapic_phys, 1);
  g_lapic_base = reinterpret_cast<volatile uint32*>(virtual_addr);

  // Mask the PIT IRQ on the legacy PIC (IRQ 0)
  uint8 pic1_mask = ReadIOByte(0x21);
  WriteIOByte(0x21, pic1_mask | 0x01);

  // Enable the Local APIC (SVR = 0xF0) with spurious vector 0xFF
  WriteLapicRegister(kLapicSpuriousInterruptVectorRegister, 0xFF | (1 << 8));
}

void CalibrateLapicTimer() {
  // Set divisor to divide-by-16
  WriteLapicRegister(kLapicTimerDivideConfigurationRegister, 3);

  // Mask the LAPIC timer register
  WriteLapicRegister(kLapicTimerLvtRegister, kLapicTimerMasked);

  // Set initial count to maximum (0xFFFFFFFF)
  WriteLapicRegister(kLapicTimerInitialCountRegister, 0xFFFFFFFF);

  // Measure a 10ms window using the TSC
  uint64 tsc_start = ReadTimestampCounter();
  uint64 tsc_target = tsc_start + (10000 * g_tsc_ticks_per_microsecond);

  while (ReadTimestampCounter() < tsc_target) {
    // Busy loop
  }

  // Read remaining count and calculate ticks elapsed
  uint32 lapic_end = ReadLapicRegister(kLapicTimerCurrentCountRegister);
  uint32 elapsed_ticks = 0xFFFFFFFF - lapic_end;

  g_lapic_ticks_per_microsecond = elapsed_ticks / 10000;
  if (g_lapic_ticks_per_microsecond == 0) g_lapic_ticks_per_microsecond = 1;

  // Stop the timer for now
  WriteLapicRegister(kLapicTimerInitialCountRegister, 0);

  print << "LAPIC Timer Calibrated: " << g_lapic_ticks_per_microsecond
        << " ticks/microsecond\n";
}

void SetLapicTimerOneShot(size_t microseconds) {
  if (microseconds == 0) microseconds = 1;
  // Set timer to APIC timer vector, One-Shot Mode, Unmasked
  WriteLapicRegister(kLapicTimerLvtRegister, kApicTimerInterruptVector);

  uint64 ticks = microseconds * g_lapic_ticks_per_microsecond;
  if (ticks > 0xFFFFFFFF) ticks = 0xFFFFFFFF;
  WriteLapicRegister(kLapicTimerInitialCountRegister,
                     static_cast<uint32>(ticks));
}

void DisableLapicTimer() {
  WriteLapicRegister(kLapicTimerLvtRegister, kLapicTimerMasked);
  WriteLapicRegister(kLapicTimerInitialCountRegister, 0);
}
#endif

}  // namespace

void TimerHandler() {
#ifndef TEST
  size_t now = GetCurrentTimestampInMicroseconds();
  size_t prev_time = microseconds_since_kernel_started;
  size_t delta_time = now > prev_time ? now - prev_time : 0;
  microseconds_since_kernel_started = now;

#ifdef VERBOSE_POLLING
  // Periodic process activity dump to debug freezes
  static size_t last_dump_timestamp = 0;
  if (microseconds_since_kernel_started - last_dump_timestamp >= 250000) {
    last_dump_timestamp = microseconds_since_kernel_started;
    print << "--- PROCESS ACTIVITY DUMP ---\n";
    for (ProcessRef proc = GetProcessOrNextFromPid(0); proc;
         proc = GetProcessOrNextFromPid(proc->pid + 1)) {
      print << "Process: " << proc->name << " (PID: " << proc->pid << ")";
      if (proc->is_driver) print << " [Driver]";
      print << "\n";
      for (Thread* thread : proc->threads) {
        print << "  Thread TID: " << thread->id;
        if (thread->is_awake()) {
          print << " (AWAKE on core " << thread->running_on_core << " rip=" << NumberFormat::Hexidecimal << thread->registers.rip << " rsp=" << thread->registers.rsp << ")";
        } else {
          print << " (ASLEEP)";
          if (thread->is_waiting_for_message()) {
            print << " waiting for msg";
          }
          if (thread->is_waiting_for_shared_memory()) {
            print << " waiting for shm";
          }
        }
        print << " Priority: " << (size_t)thread->priority << "\n";
      }
    }
    print << "-------------------------\n";
  }
#endif

#else
  size_t delta_time = 10000;
  microseconds_since_kernel_started += delta_time;
#endif

  // Transition to the next epoch ONLY if tracking is active
#ifndef TEST
  if (IsCpuTrackingActive()) {
    InterruptSafeSpinlockGuard guard(g_timer_spinlock);
    if (now >= last_cpu_epoch_timestamp + 1000000) {
      last_cpu_epoch_timestamp = now;
      current_epoch_count++;

      // Process rolling averages ONLY for processes that were active this
      // epoch.
      while (true) {
        Process* proc = active_processes_this_epoch.PopFront();
        if (proc == nullptr) break;
        proc->is_on_active_list_this_epoch = false;
        CatchUpProcessCpuUsage(proc);
      }
    }
  }
#endif

#ifdef PROFILING_ENABLED
  if (delta_time >= microseconds_until_next_profile) {
    PrintProfilingInformation();
    microseconds_until_next_profile = kProfileIntervalInMicroseconds;
  } else {
    microseconds_until_next_profile -= delta_time;
  }
#endif

  // Call any timer events that are scheduled to run.
  while (true) {
    TimerEvent* timer_event = nullptr;
    {
      InterruptSafeSpinlockGuard guard(g_timer_spinlock);
      TimerEvent* first = scheduled_timer_events.FirstItem();
      if (first != nullptr &&
          first->timestamp_to_trigger_at <= microseconds_since_kernel_started) {
        scheduled_timer_events.Remove(first);
        first->process_to_send_message_to->timer_events.Remove(first);
        if (first->process_to_send_message_to->timer_event_count > 0) {
          first->process_to_send_message_to->timer_event_count--;
        }
        timer_event = first;
      }
    }
    if (timer_event == nullptr) break;

    // Send the message to the process.
    ipc::SendKernelMessageToProcess(timer_event->process_to_send_message_to,
                                    timer_event->message_id_to_send, 0, 0, 0, 0,
                                    0);

    // Release the memory for the TimerEvent.
    ObjectPool<TimerEvent>::Release(timer_event);
  }

  ScheduleNextThread();

  ReprogramTimerForNextDeadline();
}

// Initializes the timer.
void InitializeTimer() {
  microseconds_since_kernel_started = 0;
  new (&scheduled_timer_events)
      AATree<TimerEvent, &TimerEvent::node_in_all_timer_events,
             &TimerEvent::timestamp_to_trigger_at>();
  new (&active_processes_this_epoch)
      LinkedList<Process, &Process::node_active_this_epoch>();
  new (&processes_subscribing_to_cpu_tracking)
      LinkedList<Process, &Process::node_cpu_tracking_subscription>();

  utc_offset = 0;
#ifndef TEST
  CalibrateTsc();
  InitializeLapic();
  CalibrateLapicTimer();
  SetLapicTimerOneShot(10000);
#endif
  new (&time_info_change_subscriptions)
      LinkedList<TimeInfoChangeSubscription,
                 &TimeInfoChangeSubscription::node>();

#ifdef PROFILING_ENABLED
  microseconds_until_next_profile = kProfileIntervalInMicroseconds;
#endif
}

// Returns the current time, in microseconds, since the kernel has started.
size_t GetCurrentTimestampInMicroseconds() {
#ifdef TEST
  return microseconds_since_kernel_started;
#else
  uint64 current_tsc = ReadTimestampCounter();
  if (current_tsc < boot_tsc_value) {
    return 0;
  }
  return (current_tsc - boot_tsc_value) / g_tsc_ticks_per_microsecond;
#endif
}

// Sends a message to the process at or after a specified number of microseconds
// have elapsed since the kernel started.
void SendMessageToProcessAtMicroseconds(Process* process, size_t timestamp,
                                        size_t message_id) {
  constexpr size_t kMaxTimerEventsPerProcess = 256;
  TimerEvent* timer_event = nullptr;
  {
    InterruptSafeSpinlockGuard guard(g_timer_spinlock);
    if (process->timer_event_count >= kMaxTimerEventsPerProcess) return;
    timer_event = ObjectPool<TimerEvent>::Allocate();
    if (timer_event == nullptr) return;

    timer_event->process_to_send_message_to = process;
    timer_event->timestamp_to_trigger_at = timestamp;
    timer_event->message_id_to_send = message_id;

    process->timer_event_count++;
    // Add to global tree of scheduled timer events.
    scheduled_timer_events.Insert(timer_event);
    // Add to process.
    process->timer_events.AddBack(timer_event);
  }

  ReprogramTimerForNextDeadline();
}

// Cancel all timer events that could be scheduled for a process.
void CancelAllTimerEventsForProcess(Process* process) {
  bool changed = false;
  while (true) {
    TimerEvent* timer_event = nullptr;
    {
      InterruptSafeSpinlockGuard guard(g_timer_spinlock);
      timer_event = process->timer_events.PopFront();
      if (timer_event != nullptr) {
        scheduled_timer_events.Remove(timer_event);
        if (process->timer_event_count > 0) process->timer_event_count--;
        changed = true;
      }
    }
    if (timer_event == nullptr) break;
    ObjectPool<TimerEvent>::Release(timer_event);
  }
  if (changed) ReprogramTimerForNextDeadline();
}

void UpdateRunningThreadTimeslice() {
#ifndef TEST
  if (RunningThread() == nullptr) return;
  size_t now = GetCurrentTimestampInMicroseconds();
  if (RunningThread()->current_run_start_timestamp == 0) {
    RunningThread()->current_run_start_timestamp = now;
    return;
  }
  if (now > RunningThread()->current_run_start_timestamp) {
    size_t elapsed = now - RunningThread()->current_run_start_timestamp;
    if (elapsed >= RunningThread()->remaining_timeslice_microseconds) {
      RunningThread()->remaining_timeslice_microseconds = 0;
    } else {
      RunningThread()->remaining_timeslice_microseconds -= elapsed;
    }

    // Execute CPU tracking ONLY if tracking is active
    if (IsCpuTrackingActive()) {
      Process* proc = RunningThread()->process;
      if (proc != nullptr && !proc->is_dying) {
        size_t core_id = GetCurrentCoreId();

        InterruptSafeSpinlockGuard guard(g_timer_spinlock);
        // Catch up the process if it was idle during previous epochs
        CatchUpProcessCpuUsage(proc);

        proc->cpu_time_in_current_epoch[core_id] += elapsed;

        // Register process on the active list for this epoch
        if (!proc->is_on_active_list_this_epoch) {
          proc->is_on_active_list_this_epoch = true;
          active_processes_this_epoch.AddBack(proc);
        }
      }
    }
  }
  RunningThread()->current_run_start_timestamp = now;
#endif
}

void ReprogramTimerForNextDeadline() {
#ifndef TEST
  size_t now = GetCurrentTimestampInMicroseconds();
  size_t next_deadline = ~0ULL;
  bool has_deadline = false;

  if (RunningThread() != nullptr && NeedsTimesliceInterrupt(RunningThread())) {
    next_deadline = RunningThread()->current_run_start_timestamp +
                    RunningThread()->remaining_timeslice_microseconds;
    has_deadline = true;
  } else if (RunningThread() == nullptr && HasAwakeThreads()) {
    next_deadline = now;
    has_deadline = true;
  }

  {
    InterruptSafeSpinlockGuard guard(g_timer_spinlock);
    TimerEvent* first_event = scheduled_timer_events.FirstItem();
    if (first_event != nullptr) {
      size_t event_time = first_event->timestamp_to_trigger_at;
      if (!has_deadline || event_time < next_deadline) {
        next_deadline = event_time;
        has_deadline = true;
      }
    }
  }

#ifdef VERBOSE_POLLING
  size_t max_duration =
      250000;  // 250ms maximum duration to ensure periodic dumps.
  if (!has_deadline) {
    SetLapicTimerOneShot(max_duration);
  } else {
    size_t duration = max_duration;
    if (next_deadline > now) {
      duration = next_deadline - now;
      if (duration > max_duration) duration = max_duration;
    } else {
      // Deadline is in the past or now, trigger immediately.
      duration = 1;
    }
    SetLapicTimerOneShot(duration);
  }
#else
  if (!has_deadline) {
    DisableLapicTimer();
  } else {
    size_t duration = 0;
    if (next_deadline > now) duration = next_deadline - now;
    SetLapicTimerOneShot(duration);
  }
#endif
#endif
}

void CatchUpProcessCpuUsage(Process* process) {
  if (process->last_updated_epoch >= current_epoch_count) {
    process->last_updated_epoch = current_epoch_count;
    return;
  }
  size_t epochs_passed = current_epoch_count - process->last_updated_epoch;
  if (epochs_passed == 0) return;

  constexpr size_t kEpochDurationInMicroseconds = 1000000;  // 1-second epoch

  for (int c = 0; c < kMaxCores; c++) {
    // Calculation the CPU usage (0 to 255) for the first completed epoch.
    size_t current_byte_val =
        (process->cpu_time_in_current_epoch[c] * 255) / kEpochDurationInMicroseconds;
    if (current_byte_val > 255) current_byte_val = 255;

    // Apply the Exponentially Weighted Moving Average to smooth out spikes.
    // 30% of the weight is given to the new value, and 70% to the old value.
    process->rolling_cpu_percentage[c] =
        (uint8)((current_byte_val * 3 +
                 process->rolling_cpu_percentage[c] * 7) /
                10);
    process->cpu_time_in_current_epoch[c] = 0;

    // Apply lazy decay for any subsequent fully idle epochs (each multiplied
    // by 0.7)
    for (size_t i = 0;
         i < epochs_passed - 1 && process->rolling_cpu_percentage[c] > 0; i++) {
      process->rolling_cpu_percentage[c] =
          (uint8)((process->rolling_cpu_percentage[c] * 7) / 10);
    }
  }

  process->last_updated_epoch = current_epoch_count;
}

size_t CalculateCompactCpuUsage(Process* process) {
  size_t packed_bytes = 0;
  for (int c = 0; c < 8 && c < kMaxCores; c++)
    packed_bytes |= ((size_t)process->rolling_cpu_percentage[c] << (c * 8));
  return packed_bytes;
}

void SetThatProcessCaresAboutCpuTracking(Process* process, bool active) {
  InterruptSafeSpinlockGuard guard(g_timer_spinlock);
  if (active == process->tracking_cpu_usage) return;
  if (active) {
    processes_subscribing_to_cpu_tracking.AddBack(process);

    // Reset/Initialize epoch metrics on the very first subscription.
    if (processes_subscribing_to_cpu_tracking.FirstItem() == process) {
      last_cpu_epoch_timestamp = GetCurrentTimestampInMicroseconds();
      current_epoch_count = 0;
      for (Process* p : processes_subscribing_to_cpu_tracking) {
        p->last_updated_epoch = 0;
      }
    }
  } else {
    processes_subscribing_to_cpu_tracking.Remove(process);
  }
  process->tracking_cpu_usage = active;
}

void RemoveProcessFromCpuTracking(Process* process) {
  SetThatProcessCaresAboutCpuTracking(process, false);

  // Remove from active epoch list if present.
  InterruptSafeSpinlockGuard guard(g_timer_spinlock);
  process->is_on_active_list_this_epoch = false;
  active_processes_this_epoch.Remove(process);
}

bool IsCpuTrackingActive() {
  InterruptSafeSpinlockGuard guard(g_timer_spinlock);
  return !processes_subscribing_to_cpu_tracking.IsEmpty();
}

void GetTimeInfo(size_t& offset, size_t& multiplier) {
  InterruptSafeSpinlockGuard guard(g_time_info_spinlock);
  offset = utc_offset;
  multiplier = g_tsc_ticks_per_microsecond;
}

void SetTimeInfo(size_t utc_microseconds) {
  InterruptSafeSpinlockGuard guard(g_time_info_spinlock);
#ifndef TEST
  uint64 current_tsc = ReadTimestampCounter();
  size_t microseconds_since_boot =
      static_cast<size_t>(current_tsc / g_tsc_ticks_per_microsecond);
  // A UTC timestamp that predates boot would underflow the unsigned offset, so
  // treat it as an offset of zero.
  utc_offset = utc_microseconds > microseconds_since_boot
                   ? utc_microseconds - microseconds_since_boot
                   : 0;
#else
  utc_offset = utc_microseconds;
#endif

  for (auto* sub : time_info_change_subscriptions) {
    ipc::SendKernelMessageToProcess(sub->process, sub->message_id, utc_offset,
                                    g_tsc_ticks_per_microsecond, 0, 0, 0);
  }
}

void RegisterMessageForWhenTimeInfoChanges(Process* process,
                                           size_t message_id) {
  constexpr size_t kMaxTimeInfoSubscriptionsPerProcess = 16;
  // Allocate before taking the lock so the heap is never entered while holding
  // it.
  auto* subscription =
      (TimeInfoChangeSubscription*)malloc(sizeof(TimeInfoChangeSubscription));
  if (subscription == nullptr) return;

  {
    InterruptSafeSpinlockGuard guard(g_time_info_spinlock);
    if (process->time_info_subscription_count >= kMaxTimeInfoSubscriptionsPerProcess) {
      free(subscription);
      return;
    }

    bool already_subscribed = false;
    for (auto* sub : time_info_change_subscriptions) {
      if (sub->process == process && sub->message_id == message_id) {
        already_subscribed = true;
        break;
      }
    }

    if (!already_subscribed) {
      subscription->process = process;
      subscription->message_id = message_id;
      process->time_info_subscription_count++;
      time_info_change_subscriptions.AddBack(subscription);
      return;
    }
  }

  free(subscription);
}

void CancelTimeInfoChangeSubscriptionsForProcess(Process* process) {
  // Unlink one subscription at a time so each is released outside of the lock.
  while (true) {
    TimeInfoChangeSubscription* to_free = nullptr;
    {
      InterruptSafeSpinlockGuard guard(g_time_info_spinlock);
      for (auto* sub : time_info_change_subscriptions) {
        if (sub->process == process) {
          time_info_change_subscriptions.Remove(sub);
          if (process->time_info_subscription_count > 0) {
            process->time_info_subscription_count--;
          }
          to_free = sub;
          break;
        }
      }
    }
    if (to_free == nullptr) return;
    free(to_free);
  }
}

}  // namespace scheduling

