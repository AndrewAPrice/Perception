#pragma once
#include "types.h"

// The programmable interrupt timer (PIT) triggers many times a second and is
// the basis of preemptive multitasking.

struct Process;

// The function that gets called each time to timer fires.
void TimerHandler();

// Initializes the timer.
void InitializeTimer();

// Returns the current time, in microseconds, since the kernel has started.
size_t GetCurrentTimestampInMicroseconds();

// Sends a message to the process at or after a specified number of microseconds
// have ellapsed since the kernel started.
void SendMessageToProcessAtMicroseconds(Process* process, size_t timestamp,
                                        size_t message_id);

// Cancel all timer events that could be scheduled for a process.
void CancelAllTimerEventsForProcess(Process* process);

// Reprograms the APIC timer to fire at the next scheduling or event deadline.
void ReprogramTimerForNextDeadline();

// Updates the currently executing thread's remaining timeslice by measuring
// elapsed TSC time.
void UpdateRunningThreadTimeslice();

// Performs lazy-evaluation catch-up and decay for process CPU rolling average.
void CatchUpProcessCpuUsage(Process* process);

// Calculates CPU usage bytes across up to 8 cores (1 byte per core).
size_t CalculateCompactCpuUsage(Process* process);

// Sets whether a process cares about CPU tracking. Tracking what is using the
// CPU is only active if at least one process cares about it.
void SetThatProcessCaresAboutCpuTracking(Process* process, bool active);

// Removes the process from CPU tracking. Call this when a process is being
// destroyed.
void RemoveProcessFromCpuTracking(Process* process);

// Returns whether CPU tracking is active.
bool IsCpuTrackingActive();

// Gets the current UTC offset and TSC multiplier.
void GetTimeInfo(size_t& offset, size_t& multiplier);

// Sets the current UTC time.
void SetTimeInfo(size_t utc_microseconds);

// Registers a process to receive a message when the time info changes.
void RegisterMessageForWhenTimeInfoChanges(Process* process, size_t message_id);

// Cancels all time info change subscriptions for a process.
void CancelTimeInfoChangeSubscriptionsForProcess(Process* process);