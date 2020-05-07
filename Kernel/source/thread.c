#include "thread.h"

#include "interrupts.h"
#include "liballoc.h"
#include "process.h"
#include "physical_allocator.h"
#include "io.h"
#include "virtual_allocator.h"
#include "scheduler.h"
#include "text_terminal.h"

size_t next_thread_id;
// struct Thread *next_thread_to_clean;
// struct Thread *thread_cleaner_thread;

extern void save_fpu_registers(size_t regs_addr);

// The kernel's idle registers.
// struct isr_regs kernel_idle_registers;

// Initialize threads.
void InitializeThreads() {
	next_thread_id = 0;
//	next_thread_to_clean = 0;

//	thread_cleaner_thread = create_thread(0, (size_t)thread_cleaner, 0);
}

// Createss a thread.
struct Thread *CreateThread(struct Process *process, size_t entry_point, size_t param) {
	//lock_interrupts();

	struct Thread *thread = malloc(sizeof(struct Thread));
	if(thread == 0) {
		return 0; /* out of memory */
	}

	/* set up the stack - grab a virtual page */	
	thread->pml4 = process ? process->pml4 : kernel_pml4;
	size_t stack = FindFreePageRange(thread->pml4, 1);
	if(stack == OUT_OF_MEMORY) {
		free(thread); /* out of memory */
	//	unlock_interrupts();
		return 0;
	}

	/* grab a physical page */
	size_t phys = GetPhysicalPage();
	if(phys == OUT_OF_PHYSICAL_PAGES) {
		free(thread); /* out of memory */
		return 0;
	}

	/* map the new stack */
	MapPhysicalPageToVirtualPage(thread->pml4, stack, phys);

	/* set up our initial registers */
	struct isr_regs *regs = malloc(sizeof(struct isr_regs));
	regs->r15 = 0; regs->r14 = 0; regs->r13 = 0; regs->r12 = 0; regs->r11 = 0; regs->r10 = 0; regs->r9 = 0; regs->r8 = 0;
	regs->rbp = stack + PAGE_SIZE; regs->rdi = param; regs->rsi = 0; regs->rdx = 0; regs->rcx = 0; regs->rbx = 0; regs->rax = 0;
	regs->rip = entry_point; regs->cs = 0x20 | 3;
	regs->eflags = 
		((!process) ? ((1 << 12) | (1 << 13)) : 0) | /* set iopl bits for kernel threads */
		(1 << 9) | /* interrupts enabled */
		(1 << 21) /* can use CPUID */; 
	regs->usersp = stack + PAGE_SIZE; regs->ss = 0x18 | 3;

	/* set up the thread object */
	thread->process = process;
	thread->stack = stack;
	thread->registers = regs;
	thread->id = next_thread_id;
	next_thread_id++;
	thread->awake = false;
	thread->awake_in_process = false;
	thread->time_slices = 0;
	/*print_string("Virtual page: ");
	print_hex(virt_page);
	asm("hlt");*/

	/* add it to the linked list of threads */
	thread->previous = 0;
	if(process->threads) {
		process->threads->previous = thread;
	}
	thread->next = process->threads;
	process->threads = thread;
	process->thread_count++;

	/* populate the fpu registers with something */
	memset(thread->fpu_registers, 0, 512);


	/* initially asleep */
	thread->next_awake = 0;
	thread->previous = 0;

	// unlock_interrupts();

	return thread;
}

/* this is a thread that cleans up threads in limbo, we have to do this from another thread, because we can't deallocate a
   thread's stack in that thread's interrupt handler */
/*void thread_cleaner() {
	while(sleep_if_not_set((size_t *)&next_thread_to_clean)) {
		// lock_interrupts();
		struct Thread *thread = next_thread_to_clean;
		if(thread) {
			next_thread_to_clean = thread->next;

			struct Process *process = thread->process;

			// release used memory
			UnmapVirtualPage(process ? process->pml4 : kernel_pml4, thread->stack, true);
			free(thread);
		}

		// unlock_interrupts();
	}
}*/

// Destroys a thread.
void DestroyThread(struct Thread *thread, bool process_being_destroyed) {
	// Make sure the thread is not scheduled.
	if (thread->awake) {
		UnscheduleThread(thread);
	}

	// Free the thread's stack.
	UnmapVirtualPage(thread->pml4, thread->stack, true);

	/* remove this thread from the process */
	struct Process *process = thread->process;
	if(thread->next != 0) {
		thread->next->previous = thread->previous;
	}

	if(thread->previous != 0) {
		thread->previous->next = thread->next;
	}
	else {
		process->threads = thread->next;
	}
	process->thread_count--;

	free(thread);

	if (process->thread_count == 0 && !process_being_destroyed) {
		DestroyProcess(process);
	}
}

// Destroys all threads for a process.
void DestroyThreadsForProcess(struct Process *process, bool process_being_destroyed) {
	while(process->threads) {
		DestroyThread(process->threads, process_being_destroyed);
	}
}

// Returns a thread with the provided tid in process, return 0 if it doesn't exist.
struct Thread* GetThreadFromTid(struct Process* process, size_t tid) {
	struct Thread* thread = process->threads;
	while (thread != 0) {
		if (thread->id == tid) {
			return thread;
		}
		thread = thread->next;
	}
	return 0;
}
