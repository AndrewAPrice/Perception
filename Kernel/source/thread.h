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

	char fpu_registers[512] __attribute__((aligned(16))); /* storage for FPU registers, must be 16 byte aligned (malloc is) */
};

// The kernel's idle registers.
extern struct isr_regs kernel_idle_registers;

// Initialize threads.
extern void InitializeThreads();

// Creates a thread for a process.
extern struct Thread *CreateThread(struct Process *process, size_t entry_point, size_t param);

// Destroys a thread.
extern void DestroyThread(struct Thread *thread, bool process_being_destroyed);

// Destroys all threads for a process.
extern void DestroyThreadsForProcess(struct Process *process, bool process_being_destroyed);

// Returns a thread with the provided tid in process, return 0 if it doesn't exist.
extern struct Thread* GetThreadFromTid(struct Process* process, size_t tid);