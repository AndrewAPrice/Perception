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

// The programmable interrupt timer (PIT) triggers many times a second and is
// the basis of preemptive multitasking.

namespace processes {
struct Process;
}

namespace scheduling {

// The function that gets called each time the timer fires.
void TimerHandler();

// Initializes the timer.
void InitializeTimer();

// Returns the current time, in microseconds, since the kernel has started.
size_t GetCurrentTimestampInMicroseconds();

// Sends a message to the process at or after a specified number of microseconds
// have elapsed since the kernel started.
void SendMessageToProcessAtMicroseconds(processes::Process* process, size_t timestamp,
                                        size_t message_id);

// Cancel all timer events that could be scheduled for a process.
void CancelAllTimerEventsForProcess(processes::Process* process);

// Reprograms the APIC timer to fire at the next scheduling or event deadline.
void ReprogramTimerForNextDeadline();

// Updates the currently executing thread's remaining timeslice by measuring
// elapsed TSC time.
void UpdateRunningThreadTimeslice();

// Performs lazy-evaluation catch-up and decay for process CPU rolling average.
void CatchUpProcessCpuUsage(processes::Process* process);

// Calculates CPU usage bytes across up to 8 cores (1 byte per core).
size_t CalculateCompactCpuUsage(processes::Process* process);

// Sets whether a process cares about CPU tracking. Tracking what is using the
// CPU is only active if at least one process cares about it.
void SetThatProcessCaresAboutCpuTracking(processes::Process* process, bool active);

// Removes the process from CPU tracking. Call this when a process is being
// destroyed.
void RemoveProcessFromCpuTracking(processes::Process* process);

// Returns whether CPU tracking is active.
bool IsCpuTrackingActive();

// Gets the current UTC offset and TSC multiplier.
void GetTimeInfo(size_t& offset, size_t& multiplier);

// Sets the current UTC time.
void SetTimeInfo(size_t utc_microseconds);

// Registers a process to receive a message when the time info changes.
void RegisterMessageForWhenTimeInfoChanges(processes::Process* process, size_t message_id);

// Cancels all time info change subscriptions for a process.
void CancelTimeInfoChangeSubscriptionsForProcess(processes::Process* process);

// Prints Local APIC registers for debugging.
void PrintLapicRegisters();

// Number of TSC cycles elapsed per microsecond.
extern uint64 g_tsc_ticks_per_microsecond;

#ifndef TEST
// Number of Local APIC timer cycles elapsed per microsecond.
extern uint64 g_lapic_ticks_per_microsecond;

// Busy-waits for a given duration in microseconds using the TSC.
void SleepMicroseconds(size_t microseconds);
#endif

}  // namespace scheduling
