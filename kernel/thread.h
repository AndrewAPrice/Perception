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
	int awake_in_process : 1; /* thread is awake in process, even if process is asleep */

	/* linked list of awake threads */
	struct Thread *next_awake;
	struct Thread *previous_awake;

	size_t pml4; /* the pml4 we're operating in, which maybe different to our process, e.g. for kernel threads */

	size_t time_slices; /* time slices this thread has had */
};

/* must be called in an interrupt handler or with interrupts disabled */
void init_threads();
struct Thread *create_thread(struct Process *process, size_t entry_point, size_t params);
void destroy_thread(struct Thread *thread);
void destroy_threads_for_process(struct Process *process);

