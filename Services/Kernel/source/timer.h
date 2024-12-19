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