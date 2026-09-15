#ifndef TEST
// Copyright 2023 Google LLC
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

#include "diagnostics/profiling.h"

#include "containers/spinlock.h"
#include "interrupts/exceptions.h"
#include "interrupts/interrupts.h"
#include "hardware/io.h"
#include "processes/process.h"
#include "scheduling/cpu_core.h"
#include "scheduling/scheduler.h"
#include "syscall/syscall.h"
#include "syscall/syscalls.h"
#include "output/text_terminal.h"
#include "scheduling/thread.h"

namespace diagnostics {

using containers::InterruptSafeSpinlock;
using containers::InterruptSafeSpinlockGuard;
using hardware::ReadTimestampCounter;
using interrupts::Exception;
using interrupts::GetExceptionName;
using interrupts::kNumberOfExceptions;
using interrupts::kNumberOfInterrupts;
using output::NumberFormat;
using output::print;
using processes::GetProcessOrNextFromPid;
using processes::Process;
using processes::ProcessRef;
using scheduling::RunningThread;
using syscall::GetSystemCallName;
using syscall::kNumberOfSyscalls;
using syscall::Syscall;

// How many times profiling is enabled. This is incremented and decremented
// everytime profiling is enabled and disabled. The results are printed when
// this reaches 0.
extern "C" size_t g_profiling_enabling_count;
size_t g_profiling_enabling_count = 0;

namespace {

// Spinlock protecting profiler enable/disable and buffer reset operations.
InterruptSafeSpinlock g_profiler_lock;

// The number of cycles spent in the kernel while profiling has been enabled.
size_t g_kernel_cycles_while_profiling_is_enabled;

// The number of cycles spent idle while profiling has been enabled.
size_t g_idle_cycles_while_profiling_is_enabled;

// The number of cycles spent in processes that have quit while profiling has
// been enabled.
size_t g_cycles_from_processes_that_quit_while_profiling_is_enabled;

constexpr int kItemsToProfile =
    kNumberOfExceptions + kNumberOfInterrupts + kNumberOfSyscalls + 1;

// Represents a kernel event (exception, interrupt, or syscall).
struct ProfilingInformation {
  size_t total_time;
  size_t count;
  size_t shortest_time;
  size_t longest_time;

  static void PrintHeader() {
    print << "count,total_time,shortest_time,average_time,longest_time\n";
  }

  void Print() {
    print << count << ',' << total_time << ','
          << (count == 0 ? 0 : shortest_time) << ','
          << (count == 0 ? 0 : (total_time / count)) << ',' << longest_time
          << '\n';
  }

  // Records an instance of the event running.
  void RecordInstance(size_t cycles) {
    // Every core records into the same table, so the counters are updated
    // atomically.
    __atomic_fetch_add(&count, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&total_time, cycles, __ATOMIC_RELAXED);

    size_t shortest = __atomic_load_n(&shortest_time, __ATOMIC_RELAXED);
    while (cycles < shortest &&
           !__atomic_compare_exchange_n(&shortest_time, &shortest, cycles,
                                        /*weak=*/true, __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
    }

    size_t longest = __atomic_load_n(&longest_time, __ATOMIC_RELAXED);
    while (cycles > longest &&
           !__atomic_compare_exchange_n(&longest_time, &longest, cycles,
                                        /*weak=*/true, __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
    }
  }
};

// A table of events to profile.
ProfilingInformation profiling_information[kItemsToProfile];

// Returns an index to profiling_information for unknown events.
int GetIndexForUnknown() {
  return kNumberOfExceptions + kNumberOfInterrupts + kNumberOfSyscalls;
}

// Returns an index to profiling_information for an exception.
int GetIndexForException(int exception) {
  if (exception < 0 || exception >= kNumberOfExceptions)
    return GetIndexForUnknown();
  return exception;
}

// Returns an index to profiling_information for an interrupt.
int GetIndexForInterrupt(int interrupt) {
  if (interrupt < 0 || interrupt >= kNumberOfInterrupts)
    return GetIndexForUnknown();
  return kNumberOfExceptions + interrupt;
}

// Returns an index to profiling_information for a syscall.
int GetIndexForSyscall(int syscall) {
  if (syscall < 0 || syscall >= kNumberOfSyscalls) return GetIndexForUnknown();
  return kNumberOfExceptions + kNumberOfInterrupts + syscall;
}

// Returns the number of cycles executed on this CPU since boot.
size_t NumberOfCPUCyclesSinceBoot() {
  return static_cast<size_t>(ReadTimestampCounter());
}

// Returns the number of cycles since last time this was called on this core.
size_t GetAndUpdateCyclesSinceLastTransition() {
  scheduling::CpuCoreState& cpu = scheduling::GetCurrentCpuCore();
  size_t now = NumberOfCPUCyclesSinceBoot();
  size_t cycles_since_last_time = now - cpu.profiling_transition_cycle;
  cpu.profiling_transition_cycle = now;
  return cycles_since_last_time;
}

// Profile the CPU entering kernel space for an event.
void ProfileEnteringKernelSpaceForEvent(int event_index) {
  scheduling::GetCurrentCpuCore().profiling_event_index = event_index;

  // The cycles since the CPU switched to user space.
  size_t cycles = GetAndUpdateCyclesSinceLastTransition();
  if (RunningThread() == nullptr) {
    // There are no running threads, so the cycles are counted as "idle" time.
    __atomic_fetch_add(&g_idle_cycles_while_profiling_is_enabled, cycles,
                       __ATOMIC_RELAXED);
  } else {
    __atomic_fetch_add(
        &RunningThread()->process->cycles_spent_executing_while_profiled, cycles,
        __ATOMIC_RELAXED);
  }
}

// Finish profiling the current kernel space event.
void FinishProfilingKernelSpaceEvent() {
  scheduling::CpuCoreState& cpu = scheduling::GetCurrentCpuCore();
  size_t cycles = GetAndUpdateCyclesSinceLastTransition();
  // Record the cycles spent in the kernel and also associate it with a
  // specific event.
  __atomic_fetch_add(&g_kernel_cycles_while_profiling_is_enabled, cycles,
                     __ATOMIC_RELAXED);
  int event_index = cpu.profiling_event_index;
  if (event_index < 0 || event_index >= kItemsToProfile)
    event_index = GetIndexForUnknown();
  profiling_information[event_index].RecordInstance(cycles);

  // Reset the event being profiled, just incase this function gets
  // called twice in a row before a new kernel event occurs.
  cpu.profiling_event_index = GetIndexForUnknown();
}

// Prints everything that was profiled.
void PrintProfilingOutput() {
  print << "\nProfiling information:\n\n"
        << "exception,name," << NumberFormat::DecimalWithoutCommas;
  ProfilingInformation::PrintHeader();
  for (int i = 0; i < kNumberOfExceptions; i++) {
    print << GetExceptionName(static_cast<Exception>(i)) << ',' << i << ',';
    profiling_information[GetIndexForException(i)].Print();
  }

  print << "\ninterrupt,name,";
  ProfilingInformation::PrintHeader();
  for (int i = 0; i < kNumberOfInterrupts; i++) {
    print << "IRQ" << i << ',' << i << ',';
    profiling_information[GetIndexForInterrupt(i)].Print();
  }

  print << "\nsyscall,name,";
  ProfilingInformation::PrintHeader();
  for (int i = 0; i < kNumberOfSyscalls; i++) {
    print << GetSystemCallName(static_cast<Syscall>(i)) << ',' << i << ',';
    profiling_information[GetIndexForSyscall(i)].Print();
  }

  print << "\nunknown,unknown,";
  ProfilingInformation::PrintHeader();
  print << "unknown,unknown,";
  profiling_information[GetIndexForUnknown()].Print();

  print << "\nprocess,cycles\n";
  print << "<idle>," << g_idle_cycles_while_profiling_is_enabled << '\n';
  print << "<kernel>," << g_kernel_cycles_while_profiling_is_enabled << '\n';
  for (ProcessRef process = GetProcessOrNextFromPid(0); process;
       process = GetProcessOrNextFromPid(process->pid + 1)) {
    print << process->name << ','
          << process->cycles_spent_executing_while_profiled << '\n';
  }
  print << "<terminated processes>,"
        << g_cycles_from_processes_that_quit_while_profiling_is_enabled << '\n';
}

}  // namespace

void InitializeProfiling() { g_profiling_enabling_count = 0; }

void EnableProfiling(Process *process) {
  InterruptSafeSpinlockGuard guard(g_profiler_lock);
  process->has_enabled_profiling++;

  // Return if profiling is already enabled.
  if (__atomic_add_fetch(&g_profiling_enabling_count, 1, __ATOMIC_SEQ_CST) != 1)
    return;

  // Initialize the table of profiling events.
  memset((char *)profiling_information, 0, sizeof(profiling_information));
  for (int s = 0; s < kItemsToProfile; s++)
    profiling_information[s].shortest_time = 0xFFFFFFFFFFFFFFFF;

  g_kernel_cycles_while_profiling_is_enabled = 0;
  g_idle_cycles_while_profiling_is_enabled = 0;
  g_cycles_from_processes_that_quit_while_profiling_is_enabled = 0;

  // Every core measures against its own transition cycle, so seed them all
  // from now.
  size_t now = NumberOfCPUCyclesSinceBoot();
  for (int core = 0; core < scheduling::kMaxCores; core++) {
    scheduling::g_cpu_cores[core].profiling_transition_cycle = now;
    scheduling::g_cpu_cores[core].profiling_event_index = GetIndexForUnknown();
  }

  // Start profiling the system call for enabling events from now.
  scheduling::GetCurrentCpuCore().profiling_event_index =
      GetIndexForSyscall((int)Syscall::EnableProfiling);

  // Reset the counters of each process.
  for (ProcessRef process_to_reset = GetProcessOrNextFromPid(0);
       process_to_reset;
       process_to_reset = GetProcessOrNextFromPid(process_to_reset->pid + 1)) {
    process_to_reset->cycles_spent_executing_while_profiled = 0;
  }
}

void DisableAndOutputProfiling(Process *process) {
  InterruptSafeSpinlockGuard guard(g_profiler_lock);
  if (process->has_enabled_profiling == 0) return;
  process->has_enabled_profiling--;

  // Return if profiling isn't enabled, otherwise only the last disable prints.
  size_t count = __atomic_load_n(&g_profiling_enabling_count, __ATOMIC_SEQ_CST);
  do {
    if (count == 0) return;
  } while (!__atomic_compare_exchange_n(&g_profiling_enabling_count, &count,
                                         count - 1, /*weak=*/true,
                                         __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
  if (count != 1) return;

  // Finish profiling whatever lead to profiling being disabled.
  FinishProfilingKernelSpaceEvent();
  PrintProfilingOutput();
}

extern void NotifyProfilerThatProcessExited(Process *process) {
  while (process->has_enabled_profiling > 0) DisableAndOutputProfiling(process);

  if (__atomic_load_n(&g_profiling_enabling_count, __ATOMIC_RELAXED) > 0) {
    __atomic_fetch_add(
        &g_cycles_from_processes_that_quit_while_profiling_is_enabled,
        process->cycles_spent_executing_while_profiled, __ATOMIC_RELAXED);
  }
}

// Profiles the CPU entering kernel space for an exception.
extern "C" void ProfileEnteringKernelSpaceForException(int exception) {
  ProfileEnteringKernelSpaceForEvent(GetIndexForException(exception));
}

// Profiles the CPU entering  kernel space for an interrupt.
extern "C" void ProfileEnteringKernelSpaceForInterrupt(int interrupt) {
  ProfileEnteringKernelSpaceForEvent(GetIndexForInterrupt(interrupt));
}

// Profiles the CPU entering  kernel space for a syscall.
extern "C" void ProfileEnteringKernelSpaceForSyscall(int syscall) {
  ProfileEnteringKernelSpaceForEvent(GetIndexForSyscall(syscall));
}

// Notifies the profiler that the CPU is about to entered user space.
extern "C" void ProfileSwitchToUserSpace() {
  FinishProfilingKernelSpaceEvent();
}

}  // namespace diagnostics

#endif // TEST
