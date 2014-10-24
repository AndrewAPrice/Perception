#pragma once
#include "types.h" 

struct Process;
struct isr_regs;

struct Thread {
	struct Process *process; /* if 0, this is the kernel */
	struct isr_regs *registers; /* actually a stack to return */
	size_t id;
	size_t stack; /* virtual address of the allocated stack page */

	/* linked list of threads in the process */
	struct Thread *next;
	struct Thread *previous;

	int awake : 1; /* is this thread awake? */

	/* linked list of awake threads */
	struct Thread *next_awake;
	struct Thread *previous_awake;
};

/* must be called in an interrupt handler or with interrupts disabled */
void init_threads();
struct Thread *create_thread(struct Process *process, size_t entry_point, size_t params);
void destroy_thread(struct Thread *thread);
void destroy_threads_for_process(struct Process *process);

