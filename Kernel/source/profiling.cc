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

#include "profiling.h"

#include "exceptions.h"
#include "interrupts.h"
#include "io.h"
#include "process.h"
#include "scheduler.h"
#include "syscall.h"
#include "syscalls.h"
#include "text_terminal.h"
#include "thread.h"

// How many times profiling is enabled. This is incremented and decremented
// everytime profiling is enabled and disabled. The results are printed when
// this reaches 0.
extern "C" size_t profiling_enabling_count;
size_t profiling_enabling_count = 0;

namespace {
// The number of cycles spent in the kernel while profiling has been enabled.
size_t kernel_cycles_while_profiling_is_enabled;

// The number of cycles spent idle while profiling has been enabled.
size_t idle_cycles_while_profiling_is_enabled;

// The number of cycles spent in processes that have quit while profiling has
// been enabled.
size_t cycles_from_processes_that_quit_while_profiling_is_enabled;

// The cycle stamp when last transitioning between user and kernel spaces.
size_t user_kernel_space_transition_cycle;

// The index ointo profiling_information of the kernel event being profiled.
int kernel_event_being_profiled;

constexpr int kItemsToProfile =
    NUMBER_OF_EXCEPTIONS + NUMBER_OF_INTERRUPTS + NUMBER_OF_SYSCALLS + 1;

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
    count++;
    total_time += cycles;
    if (cycles < shortest_time) shortest_time = cycles;
    if (cycles > longest_time) longest_time = cycles;
  }
};

// A table of events to profile.
ProfilingInformation profiling_information[kItemsToProfile];

// Returns an index to profiling_information for unknown events.
int GetIndexForUnknown() {
  return NUMBER_OF_EXCEPTIONS + NUMBER_OF_INTERRUPTS + NUMBER_OF_SYSCALLS;
}

// Returns an index to profiling_information for an exception.
int GetIndexForException(int exception) {
  if (exception > NUMBER_OF_EXCEPTIONS) return GetIndexForUnknown();
  return exception;
}

// Returns an index to profiling_information for an interrupt.
int GetIndexForInterrupt(int interrupt) {
  if (interrupt > NUMBER_OF_INTERRUPTS) return GetIndexForUnknown();
  return NUMBER_OF_EXCEPTIONS + interrupt;
}

// Returns an index to profiling_information for a syscall.
int GetIndexForSyscall(int syscall) {
  if (syscall > NUMBER_OF_SYSCALLS) return GetIndexForUnknown();
  return NUMBER_OF_EXCEPTIONS + NUMBER_OF_INTERRUPTS + syscall;
}

// Returns the number of cycles executed on this CPU since boot.
size_t NumberOfCPUCyclesSinceBoot() {
  unsigned hi, lo;
#ifndef __TEST__
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
#endif
  return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

// Returns the number of cycles since last time this was called.
size_t GetAndUpdateCyclesSinceLastTransition() {
  size_t now = NumberOfCPUCyclesSinceBoot();
  size_t cycles_since_last_time = now - user_kernel_space_transition_cycle;
  user_kernel_space_transition_cycle = now;
  return cycles_since_last_time;
}

// Profile the CPU entering kernel space for an event.
void ProfileEnteringKernelSpaceForEvent(int event_index) {
  kernel_event_being_profiled = event_index;

  // The cycles since the CPU switched to user space.
  size_t cycles = GetAndUpdateCyclesSinceLastTransition();
  if (running_thread == nullptr) {
    // There are no running threads, so the cycles are counted as "idle" time.
    idle_cycles_while_profiling_is_enabled += cycles;
  } else {
    running_thread->process->cycles_spent_executing_while_profiled += cycles;
  }
}

// Finish profiling the current kernel space event.
void FinishProfilingKernelSpaceEvent() {
  size_t cycles = GetAndUpdateCyclesSinceLastTransition();
  // Record the cycles spent in the kernel and also associate it with a
  // specific event.
  kernel_cycles_while_profiling_is_enabled += cycles;
  profiling_information[kernel_event_being_profiled].RecordInstance(cycles);

  // Reset the event being profiled, just incase this function gets
  // called twice in a row before a new kernel event occurs.
  kernel_event_being_profiled = GetIndexForUnknown();
}

// Prints everything that was profiled.
void PrintProfilingOutput() {
  print << "\nProfiling information:\n\n"
        << "exception,name," << NumberFormat::DecimalWithoutCommas;
  ProfilingInformation::PrintHeader();
  for (int i = 0; i < NUMBER_OF_EXCEPTIONS; i++) {
    print << GetExceptionName(static_cast<Exception>(i)) << ',' << i << ',';
    profiling_information[GetIndexForException(i)].Print();
  }

  print << "\ninterrupt,name,";
  ProfilingInformation::PrintHeader();
  for (int i = 0; i < NUMBER_OF_INTERRUPTS; i++) {
    print << "IRQ" << i << ',' << i << ',';
    profiling_information[GetIndexForInterrupt(i)].Print();
  }

  print << "\nsyscall,name,";
  ProfilingInformation::PrintHeader();
  for (int i = 0; i < NUMBER_OF_SYSCALLS; i++) {
    print << GetSystemCallName(static_cast<Syscall>(i)) << ',' << i << ',';
    profiling_information[GetIndexForSyscall(i)].Print();
  }

  print << "\nunknown,unknown,";
  ProfilingInformation::PrintHeader();
  print << "unknown,unknown,";
  profiling_information[GetIndexForUnknown()].Print();

  print << "\nprocess,cycles\n";
  print << "<idle>," << idle_cycles_while_profiling_is_enabled << '\n';
  print << "<kernel>," << kernel_cycles_while_profiling_is_enabled << '\n';
  Process *process = GetProcessOrNextFromPid(0);
  while (process != nullptr) {
    print << process->name << ','
          << process->cycles_spent_executing_while_profiled << '\n';
    process = process->next;
  }
  print << "<terminated processes>,"
        << cycles_from_processes_that_quit_while_profiling_is_enabled << '\n';
}

}  // namespace

void InitializeProfiling() { profiling_enabling_count = 0; }

void EnableProfiling(Process *process) {
  process->has_enabled_profiling++;
  profiling_enabling_count++;

  // Return if profiling is already enabled.
  if (profiling_enabling_count != 1) return;

  // Initialize the table of profiling events.
  memset((char *)profiling_information, 0, sizeof(profiling_information));
  for (int s = 0; s < kItemsToProfile; s++)
    profiling_information[s].shortest_time = 0xFFFFFFFFFFFFFFFF;

  kernel_cycles_while_profiling_is_enabled = 0;
  idle_cycles_while_profiling_is_enabled = 0;
  cycles_from_processes_that_quit_while_profiling_is_enabled = 0;

  // Start profiling the system call for enabling events from now.
  kernel_event_being_profiled =
      GetIndexForSyscall((int)Syscall::EnableProfiling);
  user_kernel_space_transition_cycle = NumberOfCPUCyclesSinceBoot();

  // Reset the counters of each process.
  Process *process_to_reset = GetProcessOrNextFromPid(0);
  while (process_to_reset != nullptr) {
    process_to_reset->cycles_spent_executing_while_profiled = 0;
    process_to_reset = process_to_reset->next;
  }
}

void DisableAndOutputProfiling(Process *process) {
  if (process->has_enabled_profiling == 0) return;
  process->has_enabled_profiling--;

  if (profiling_enabling_count == 0) return;
  profiling_enabling_count--;

  // Return if profiling isn't enabled.
  if (profiling_enabling_count != 0) return;

  // Finish profiling whatever lead to profiling being disabled.
  FinishProfilingKernelSpaceEvent();
  PrintProfilingOutput();
}

extern void NotifyProfilerThatProcessExited(Process *process) {
  while (process->has_enabled_profiling > 0) DisableAndOutputProfiling(process);

  if (profiling_enabling_count > 0) {
    cycles_from_processes_that_quit_while_profiling_is_enabled +=
        process->cycles_spent_executing_while_profiled;
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
