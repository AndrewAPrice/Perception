#pragma once
#include "types.h" 

struct Thread;
struct isr_regs;

extern struct Thread *running_thread;

extern void init_scheduler();
extern struct isr_regs *schedule_next(struct isr_regs *regs); /* called from the timer interrupt */

extern void schedule_thread(struct Thread *thread);
extern void unschedule_thread(struct Thread *thread);
