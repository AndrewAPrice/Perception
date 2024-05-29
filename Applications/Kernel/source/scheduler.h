#pragma once
#include "types.h"

struct Thread;
struct Registers;

// The currently running thread.
extern Thread *running_thread;

// Currently executing registers.
extern Registers *currently_executing_thread_regs;

// Initializes the scheduler.
extern void InitializeScheduler();

// Schedule the next thread, called from the timer inerrupt.
extern void ScheduleNextThread();

extern void ScheduleThread(Thread *thread);
extern void UnscheduleThread(Thread *thread);

// Schedules a thread if we are currently halted - such as an interrupt
// woke up a thread.
extern void ScheduleThreadIfWeAreHalted();