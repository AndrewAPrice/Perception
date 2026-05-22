#pragma once
#include "types.h"

struct Thread;
struct Registers;

// The currently running thread.
extern Thread *running_thread;

// Currently executing registers.
extern Registers *currently_executing_thread_regs;

// Initializes the scheduler.
void InitializeScheduler();

// Schedule the next thread, called from the timer inerrupt.
void ScheduleNextThread();

void ScheduleThread(Thread *thread);
void UnscheduleThread(Thread *thread);

// Returns whether there are at least two awake threads in the scheduler.
bool HasAtLeast2AwakeThreads();

// Schedules a thread if we are currently halted - such as an interrupt
// woke up a thread.
void ScheduleThreadIfWeAreHalted();