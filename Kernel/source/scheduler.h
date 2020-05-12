#pragma once
#include "types.h" 

struct Thread;
struct isr_regs;

// The currently running thread.
extern struct Thread *running_thread;

// Currently executing registers.
extern struct isr_regs *currently_executing_thread_regs;

// Initializes the scheduler.
extern void InitializeScheduler();

// Schedule the next thread, called from the timer inerrupt.
extern void ScheduleNextThread();

extern void ScheduleThread(struct Thread *thread);
extern void UnscheduleThread(struct Thread *thread);

// Schedules a thread if we are currently halted - such as an interrupt
// woke up a thread.
extern void ScheduleThreadIfWeAreHalted();