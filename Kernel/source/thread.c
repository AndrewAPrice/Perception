#include "thread.h"

#include "liballoc.h"
#include "process.h"
#include "physical_allocator.h"
#include "io.h"
#include "registers.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

// The model specific register that stores the FS segment's base address.
#define FSBASE_MSR 0xC0000100

// The number of stack pages.
#define STACK_PAGES 4

size_t next_thread_id;

extern void save_fpu_registers(size_t regs_addr);

// Initialize threads.
void InitializeThreads() {
	// Clears our linked list.
	next_thread_id = 0;
}

// Createss a thread.
struct Thread *CreateThread(struct Process *process, size_t entry_point, size_t param) {
	struct Thread *thread = malloc(sizeof(struct Thread));
	if(thread == NULL) {
		return NULL;
	}

	thread->process = process;

	// Give this thread a unique ID. TODO: Make this a unique ID within the process.
	thread->id = next_thread_id;
	next_thread_id++;

	// Sets up the stack by:
	// 1) Finds a free page in the process's virtual address space.
	thread->stack = FindFreePageRange(thread->process->pml4, STACK_PAGES);
	if(thread->stack == OUT_OF_MEMORY) {
		free(thread);
		return NULL;
	}

	for (int stack_page = 0; stack_page < STACK_PAGES; stack_page++) {
		// 2) Grabs a physical page.
		size_t stack_physical_addr = GetPhysicalPage();
		if(stack_physical_addr == OUT_OF_PHYSICAL_PAGES) {
			// Free any stack pages allocated.
			for (int allocated_stack_page = 0;
				allocated_stack_page < stack_page;
				allocated_stack_page++) {
				UnmapVirtualPage(thread->process->pml4,
					thread->stack + allocated_stack_page * PAGE_SIZE, true);
			}
			free(thread);
			return NULL;
		}

		// 3) Maps the process's virtual address to the physical page.
		MapPhysicalPageToVirtualPage(thread->process->pml4,
			thread->stack + stack_page * PAGE_SIZE, stack_physical_addr, true);
	}

	// Sets up the registers that our process will start with.
	struct Registers *regs = malloc(sizeof(struct Registers));
	thread->registers = regs;

	// Initialize our general purpose registers to 0.
	regs->r15 = 0; regs->r14 = 0; regs->r13 = 0; regs->r12 = 0; regs->r11 = 0; regs->r10 = 0; regs->r9 = 0; regs->r8 = 0;
	regs->rsi = 0; regs->rdx = 0; regs->rcx = 0; regs->rbx = 0; regs->rax = 0;

	// We'll pass a parameter into 'rdi' (this can be used as a function argument.)
	regs->rdi = param;

	// Sets the instruction pointer to our entry point.
	regs->rip = entry_point;

	// Sets the stack pointer and stack base to the top of our stack. (Stacks grow down!)
	regs->rbp = regs->rsp = thread->stack + PAGE_SIZE * STACK_PAGES;

	// Sets our code and stack segment selectors (the segments are defined in Gdt64 in boot.asm)
	regs->cs = 0x20 | 3; // '| 3' means ring 3. This is a user code, not kernel code.
	regs->ss = 0x18 | 3; // Likewise with user data, not kernel data.

	// No thread segment.
	thread->thread_segment_offset = (size_t)NULL;

	// Sets up the processor's flags.
	regs->rflags = 
		((process->is_driver) ? ((1 << 12) | (1 << 13)) : 0) | // Sets the IOPL bits for drivers.
		(1 << 9) | // Interrupts are enabled.
		(1 << 21); // The thread can use CPUID.

	// The thread isn't initially awake until we schedule it.
	thread->awake = false;
	thread->next_awake = NULL;
	thread->previous_awake = NULL;

	// The thread hasn't ran for any time slices yet.
	thread->time_slices = 0;

	// The thread isn't sleeping waiting for messages.
	thread->thread_is_waiting_for_message = false;
	thread->next_thread_sleeping_for_messages = NULL;

	// Add this to the linked list of threads in the process.
	thread->previous = 0;
	if(process->threads) {
		process->threads->previous = thread;
	}
	thread->next = process->threads;
	process->threads = thread;

	// Increment the process's thread cont.
	process->thread_count++;

	// Populate the FPU registers with something.
	memset(thread->fpu_registers, 0, 512);

	thread->address_to_clear_on_termination = 0;

	return thread;
}

// Destroys a thread.
void DestroyThread(struct Thread *thread, bool process_being_destroyed) {
	// Make sure the thread is not scheduled.
	if (thread->awake) {
		UnscheduleThread(thread);
	}

	// Free the thread's stack.
	UnmapVirtualPage(thread->process->pml4, thread->stack, true);

	struct Process *process = thread->process;

	// If this thread is waiting for a message, remove it from the process's
	// queue of threads waiting for messages.
	if (thread->thread_is_waiting_for_message) {
		struct Thread* previous = NULL;
		struct Thread* current = process->thread_sleeping_for_message;

		while (current != NULL) {
			struct Thread* next = current->next_thread_sleeping_for_messages;
			if (current == thread) {
				// We have found us in the list.
				if (previous == NULL) {
					process->thread_sleeping_for_message = next;
				} else {
					previous->next_thread_sleeping_for_messages = next;
				}

			}
			current = next;
		}
		thread->next_thread_sleeping_for_messages = NULL;
	}

	// Remove this thread from the process's linked list of threads.
	if(thread->next != 0) {
		thread->next->previous = thread->previous;
	}
	if(thread->previous != 0) {
		thread->previous->next = thread->next;
	} else {
		process->threads = thread->next;
	}

	// The thread has a virtual address that should be cleared.
	if (thread->address_to_clear_on_termination) {
		// Find the virtual page and offset of the address.
		size_t offset_in_page = thread->address_to_clear_on_termination & (PAGE_SIZE - 1);
		size_t page = thread->address_to_clear_on_termination - offset_in_page;

		// Get the physical page.
		size_t physical_page = GetPhysicalAddress(thread->process->pml4, offset_in_page,
			/*ignore_unowned_pages=*/ false);
		if (physical_page != OUT_OF_MEMORY) {
			// If this virtual page was actually assigned to a physical address, set our memory location to 0.
			*(uint64*)((size_t)TemporarilyMapPhysicalMemory(physical_page, 1) + offset_in_page) = 0;
		}
	}

	// Free the thread object.
	free(thread);

	// Decrease the thread count.
	process->thread_count--;

	// If no more threads are running (and we're not in the middle of destroying it already),
	// we can destroy it.
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

// Set the thread's segment offset (FS).
void SetThreadSegment(struct Thread* thread, size_t address) {
	thread->thread_segment_offset = address;

	if (thread == running_thread) {
		LoadThreadSegment(thread);
	}
}



// Load's a thread segment.
void LoadThreadSegment(struct Thread* thread) {
	wrmsr(FSBASE_MSR, thread->thread_segment_offset);
}

