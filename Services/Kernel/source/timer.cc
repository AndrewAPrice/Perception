#include "timer.h"

#include "interrupts.h"
#include "io.h"
#include "linked_list.h"
#include "messages.h"
#include "object_pool.h"
#include "process.h"
#include "profiling.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "timer_event.h"
#include "virtual_allocator.h"

// Uncomment to see periodic process activity dumps to debug freezes.
// #define VERBOSE_POLLING

namespace {

// The number of time slices (or how many times the timer triggers) per second.
#define TIME_SLICES_PER_SECOND 100
volatile size_t microseconds_since_kernel_started;
AATree<TimerEvent, &TimerEvent::node_in_all_timer_events,
       &TimerEvent::timestamp_to_trigger_at>
    scheduled_timer_events;

#ifdef PROFILING_ENABLED
#define PROFILE_INTERVAL_IN_MICROSECONDS 10000000
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

// Sets the timer to fire 'hz' times per second.
void SetTimerPhase(size_t hz) {
  size_t divisor = 1193180 / hz;
  WriteIOByte(0x43, 0b00110110);
  WriteIOByte(0x40, divisor & 0xFF);
  WriteIOByte(0x40, divisor >> 8);
}

#ifndef TEST
uint64 tsc_ticks_per_microsecond = 1;
uint64 boot_tsc_value = 0;
bool has_invariant_tsc = false;

void CalibrateTsc() {
  // Detect Invariant TSC support
  uint32 eax, ebx, ecx, edx;
  GetCpuId(0x80000007, &eax, &ebx, &ecx, &edx);
  has_invariant_tsc = (edx & (1 << 8)) != 0;

  if (!has_invariant_tsc) {
    print << "Warning: Invariant TSC not supported on this CPU!\n";
  }

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
  tsc_ticks_per_microsecond = elapsed_tsc / 10000;

  if (tsc_ticks_per_microsecond == 0) {
    tsc_ticks_per_microsecond = 1;
  }

  print << "TSC Calibrated: " << tsc_ticks_per_microsecond
        << " ticks/microsecond ("
        << (tsc_ticks_per_microsecond * 1000000) / 1000000000 << "."
        << ((tsc_ticks_per_microsecond * 1000000) % 1000000000) / 1000000
        << " GHz)\n";
}
#endif

#ifndef TEST
volatile uint32* lapic_base = nullptr;
uint64 lapic_ticks_per_microsecond = 1;

inline void WriteLapicRegister(uint32 offset, uint32 value) {
  lapic_base[offset / 4] = value;
}

inline uint32 ReadLapicRegister(uint32 offset) {
  return lapic_base[offset / 4];
}

void InitializeLapic() {
  // Map the LAPIC base address
  size_t virtual_addr = KernelAddressSpace().MapPhysicalPages(0xFEE00000, 1);
  lapic_base = reinterpret_cast<volatile uint32*>(virtual_addr);

  // Mask the PIT IRQ on the legacy PIC (IRQ 0)
  uint8 pic1_mask = ReadIOByte(0x21);
  WriteIOByte(0x21, pic1_mask | 0x01);

  // Enable the Local APIC (SVR = 0xF0) with spurious vector 0xFF
  WriteLapicRegister(0xF0, 0xFF | (1 << 8));
}

void CalibrateLapicTimer() {
  // Set divisor to divide-by-16
  WriteLapicRegister(0x3E0, 3);

  // Mask the LAPIC timer register
  WriteLapicRegister(0x320, 1 << 16);

  // Set initial count to maximum (0xFFFFFFFF)
  WriteLapicRegister(0x380, 0xFFFFFFFF);

  // Measure a 10ms window using the TSC
  uint64 tsc_start = ReadTimestampCounter();
  uint64 tsc_target = tsc_start + (10000 * tsc_ticks_per_microsecond);

  while (ReadTimestampCounter() < tsc_target) {
    // Busy loop
  }

  // Read remaining count and calculate ticks elapsed
  uint32 lapic_end = ReadLapicRegister(0x390);
  uint32 elapsed_ticks = 0xFFFFFFFF - lapic_end;

  lapic_ticks_per_microsecond = elapsed_ticks / 10000;
  if (lapic_ticks_per_microsecond == 0) {
    lapic_ticks_per_microsecond = 1;
  }

  // Stop the timer for now
  WriteLapicRegister(0x380, 0);

  print << "LAPIC Timer Calibrated: " << lapic_ticks_per_microsecond
        << " ticks/microsecond\n";
}

void SetLapicTimerOneShot(size_t microseconds) {
  if (microseconds == 0) {
    microseconds = 1;
  }
  // Set timer to Vector 48, One-Shot Mode, Unmasked
  WriteLapicRegister(0x320, 48);

  uint64 ticks = microseconds * lapic_ticks_per_microsecond;
  if (ticks > 0xFFFFFFFF) {
    ticks = 0xFFFFFFFF;
  }
  WriteLapicRegister(0x380, static_cast<uint32>(ticks));
}

void DisableLapicTimer() {
  WriteLapicRegister(0x320, 1 << 16);
  WriteLapicRegister(0x380, 0);
}
#endif

}  // namespace

// The function that gets called each time to timer fires.
void TimerHandler() {
#ifndef TEST
  size_t now = GetCurrentTimestampInMicroseconds();
  size_t delta_time = now - microseconds_since_kernel_started;
  microseconds_since_kernel_started = now;

#ifdef VERBOSE_POLLING
  // Periodic process activity dump to debug freezes
  static size_t last_dump_timestamp = 0;
  if (microseconds_since_kernel_started - last_dump_timestamp >= 250000) {
    last_dump_timestamp = microseconds_since_kernel_started;
    print << "--- PROCESS ACTIVITY DUMP ---\n";
    for (Process* proc = GetNextProcess(nullptr); proc != nullptr;
         proc = GetNextProcess(proc)) {
      print << "Process: " << proc->name << " (PID: " << proc->pid << ")";
      if (proc->is_driver) print << " [Driver]";
      print << "\n";
      for (Thread* thread : proc->threads) {
        print << "  Thread TID: " << thread->id;
        if (thread->awake) {
          print << " (AWAKE)";
        } else {
          print << " (ASLEEP)";
          if (thread->thread_is_waiting_for_message) {
            print << " waiting for msg";
          }
          if (thread->thread_is_waiting_for_shared_memory) {
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
  size_t delta_time = (1000000 / TIME_SLICES_PER_SECOND);
  microseconds_since_kernel_started += delta_time;
#endif

  // Transition to the next epoch ONLY if tracking is active
#ifndef TEST
  if (IsCpuTrackingActive()) {
    if (now - last_cpu_epoch_timestamp >= 1000000) {
      last_cpu_epoch_timestamp = now;
      current_epoch_count++;

      // Process rolling averages ONLY for processes that were active this
      // epoch.
      while (Process* proc = active_processes_this_epoch.PopFront()) {
        proc->is_on_active_list_this_epoch = false;
        CatchUpProcessCpuUsage(proc);
      }
    }
  }
#endif

#ifdef PROFILING_ENABLED
  if (delta_time >= microseconds_until_next_profile) {
    PrintProfilingInformation();
    microseconds_until_next_profile = PROFILE_INTERVAL_IN_MICROSECONDS;
  } else {
    microseconds_until_next_profile -= delta_time;
  }
#endif

  // Call any timer events that are scheduled to run.
  while (true) {
    TimerEvent* timer_event = scheduled_timer_events.FirstItem();
    if (timer_event == nullptr || timer_event->timestamp_to_trigger_at >
                                      microseconds_since_kernel_started) {
      // Timer events are sorted, so terminate early after encountering the
      // first timer event that should not yet be triggered.
      break;
    }
    scheduled_timer_events.Remove(timer_event);

    // Send the message to the process.
    SendKernelMessageToProcess(timer_event->process_to_send_message_to,
                               timer_event->message_id_to_send, 0, 0, 0, 0, 0);

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
  SetTimerPhase(TIME_SLICES_PER_SECOND);

#ifndef TEST
  CalibrateTsc();
  InitializeLapic();
  CalibrateLapicTimer();
  SetLapicTimerOneShot(10000);
#endif

#ifdef PROFILING_ENABLED
  microseconds_until_next_profile = PROFILE_INTERVAL_IN_MICROSECONDS;
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
  return (current_tsc - boot_tsc_value) / tsc_ticks_per_microsecond;
#endif
}

// Sends a message to the process at or after a specified number of microseconds
// have ellapsed since the kernel started.
void SendMessageToProcessAtMicroseconds(Process* process, size_t timestamp,
                                        size_t message_id) {
  TimerEvent* timer_event = ObjectPool<TimerEvent>::Allocate();
  if (timer_event == nullptr) return;

  timer_event->process_to_send_message_to = process;
  timer_event->timestamp_to_trigger_at = timestamp;
  timer_event->message_id_to_send = message_id;

  // Add to global tree of scheduled timer events.
  scheduled_timer_events.Insert(timer_event);
  // Add to process.
  process->timer_events.AddBack(timer_event);

  ReprogramTimerForNextDeadline();
}

// Cancel all timer events that could be scheduled for a process.
void CancelAllTimerEventsForProcess(Process* process) {
  bool changed = false;
  while (TimerEvent* timer_event = process->timer_events.PopFront()) {
    scheduled_timer_events.Remove(timer_event);
    ObjectPool<TimerEvent>::Release(timer_event);
    changed = true;
  }
  if (changed) ReprogramTimerForNextDeadline();
}

extern "C" void SendLapicEoi() {
#ifndef TEST
  if (lapic_base != nullptr) {
    lapic_base[0xB0 / 4] = 0;
  }
#endif
}

void UpdateRunningThreadTimeslice() {
#ifndef TEST
  if (running_thread == nullptr) return;
  size_t now = GetCurrentTimestampInMicroseconds();
  if (running_thread->current_run_start_timestamp == 0) {
    running_thread->current_run_start_timestamp = now;
    return;
  }
  if (now > running_thread->current_run_start_timestamp) {
    size_t elapsed = now - running_thread->current_run_start_timestamp;
    if (elapsed >= running_thread->remaining_timeslice_microseconds) {
      running_thread->remaining_timeslice_microseconds = 0;
    } else {
      running_thread->remaining_timeslice_microseconds -= elapsed;
    }

    // Execute CPU tracking ONLY if tracking is active
    if (IsCpuTrackingActive()) {
      size_t core_id = 0;  // Single-core currently
      Process* proc = running_thread->process;

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
  running_thread->current_run_start_timestamp = now;
#endif
}

void ReprogramTimerForNextDeadline() {
#ifndef TEST
  size_t now = GetCurrentTimestampInMicroseconds();
  size_t next_deadline = 0;

  if (running_thread != nullptr && NeedsTimesliceInterrupt(running_thread)) {
    next_deadline = running_thread->current_run_start_timestamp +
                    running_thread->remaining_timeslice_microseconds;
  }

  TimerEvent* first_event = scheduled_timer_events.FirstItem();
  if (first_event != nullptr) {
    size_t event_time = first_event->timestamp_to_trigger_at;
    if (next_deadline == 0 || event_time < next_deadline) {
      next_deadline = event_time;
    }
  }

#ifdef VERBOSE_POLLING
  size_t max_duration =
      250000;  // 250ms maximum duration to ensure periodic dumps.
  if (next_deadline == 0) {
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
  if (next_deadline == 0) {
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
  size_t epochs_passed = current_epoch_count - process->last_updated_epoch;
  if (epochs_passed == 0) return;

  size_t epoch_duration = 1000000;  // 1-second epoch

  for (int c = 0; c < MAX_CORES; c++) {
    // Calculation the CPU usage (0 to 255) for the first completed epoch.
    size_t current_byte_val =
        (process->cpu_time_in_current_epoch[c] * 255) / epoch_duration;
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
  for (int c = 0; c < MAX_CORES; c++) {
    packed_bytes |= ((size_t)process->rolling_cpu_percentage[c] << (c * 8));
  }
  return packed_bytes;
}

void SetThatProcessCaresAboutCpuTracking(Process* process, bool active) {
  if (active == process->tracking_cpu_usage) return;
  if (active) {
    processes_subscribing_to_cpu_tracking.AddBack(process);

    // Reset/Initialize epoch metrics on the very first subscription.
    if (processes_subscribing_to_cpu_tracking.FirstItem() == process) {
      last_cpu_epoch_timestamp = GetCurrentTimestampInMicroseconds();
      current_epoch_count = 0;
    }
  } else {
    processes_subscribing_to_cpu_tracking.Remove(process);
  }
  process->tracking_cpu_usage = active;
}

void RemoveProcessFromCpuTracking(Process* process) {
  SetThatProcessCaresAboutCpuTracking(process, false);

  // Remove from active epoch list if present.
  if (process->is_on_active_list_this_epoch)
    active_processes_this_epoch.Remove(process);
}

bool IsCpuTrackingActive() {
  return !processes_subscribing_to_cpu_tracking.IsEmpty();
}
