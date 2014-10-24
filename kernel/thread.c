#pragma once
#include "types.h" 

struct Process;
struct isr_regs;

struct Thread {
	struct Process *process;
	struct isr_regs *registers; /* actually a stack to return */
	size_t thread_id;

	struct Thread *next;
	struct Thread *previous;

	struct Thread *next_awake;
	struct Thread *previous_awake;
}

/* must be called in an interrupt handler or with interrupts disabled */
void init_threads();
Thread *create_thread(Process *process, size_t entry_point, size_t params);
void destroy_thread(Thread *thread);
void destroy_threads_for_process(Process *process);

