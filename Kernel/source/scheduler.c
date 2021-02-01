#include "scheduler.h"

#include "interrupts.h"
#include "liballoc.h"
#include "thread.h"
#include "process.h"
#include "registers.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

// Uncomment for debug printing.
// #define DEBUG

// Linked list of awake threads we can cycle through.
struct Thread *first_awake_thread;
struct Thread *last_awake_thread;

// The currently executing thread. This can be NULL if all threads are asleep.
struct Thread *running_thread;

// The idle registers to return to when no thread is awake. (This points us to
// the while(true) {hlt} in kmain.)
struct Registers *idle_regs;

// Currently executing registers.
struct Registers *currently_executing_thread_regs;

void InitializeScheduler() {
	first_awake_thread = NULL;
	last_awake_thread = NULL;
	running_thread = NULL;
	currently_executing_thread_regs = malloc(sizeof(struct Registers));
	if (!currently_executing_thread_regs) {
		PrintString("Could not allocate object to store the kernel's registers.");
		__asm__ __volatile__("cli");
		__asm__ __volatile__("hlt");
	}
	idle_regs = currently_executing_thread_regs;

}

// Initializes the scheduler.
void ScheduleNextThread() {
	// The next thread to switch to.
	struct Thread *next;

	if(running_thread) {
		// We were currently executing a thread.
#ifdef DEBUG
		PrintString("Leaving tid "); PrintNumber(running_thread->id);
		PrintString(" pid "); PrintNumber(running_thread->process->pid);
		PrintChar('\n');
		PrintRegisters(currently_executing_thread_regs);
#endif
		if (running_thread->uses_fpu_registers) {
			asm volatile("fxsave %0"::"m"(*running_thread->fpu_registers));
		}

		// Move to the next awake thread.
		next = running_thread->next_awake;
		if(!next) {
			// We reached the end of the line. Switch back to the first awake thread.
			next = first_awake_thread;
		}

	} else {
		// We were in the kernel's idle thread. Attempt to switch to the first awake thread.
		next = first_awake_thread;
	}

	if (!next) {
		// If there's no next thread, we'll return to the kernel's idle thread.
		running_thread = 0;
		currently_executing_thread_regs = idle_regs;
		SwitchToAddressSpace(kernel_pml4);
#ifdef DEBUG
		PrintString("Kernel idle thread\n");
#endif
		return;
	}

	/* enter the next thread */
	running_thread = next;
	running_thread->time_slices++;

	SwitchToAddressSpace(running_thread->process->pml4);

	if (running_thread->uses_fpu_registers) {
		asm volatile("fxrstor %0"::"m"(*running_thread->fpu_registers));
	}
	LoadThreadSegment(running_thread);

	currently_executing_thread_regs = running_thread->registers;

#ifdef DEBUG
	PrintString("Entering tid "); PrintNumber(running_thread->id);
	PrintString(" pid "); PrintNumber(running_thread->process->pid);
	PrintString(" for time "); PrintNumber(running_thread->time_slices++);
	PrintChar('\n');
	PrintRegisters(currently_executing_thread_regs);
	PrintChar('\n');
#endif
}

void ScheduleThread(struct Thread *thread) {
	//lock_interrupts();
	if(thread->awake) {
	//	unlock_interrupts();
		return;
	}

	thread->awake = true;

	thread->next_awake = 0;
	thread->previous_awake = last_awake_thread;

	if(last_awake_thread) {
		last_awake_thread->next_awake = thread;
		last_awake_thread = thread;
	} else {
		first_awake_thread = thread;
		last_awake_thread = thread;
	}
	//unlock_interrupts();
}

void UnscheduleThread(struct Thread *thread) {
	if(!thread->awake) {
		return;
	}

	thread->awake = false;

	if(thread->next_awake) {
		thread->next_awake->previous_awake = thread->previous_awake;
	} else {
		last_awake_thread = thread->previous_awake;
	}

	if(thread->previous_awake) {
		thread->previous_awake->next_awake = thread->next_awake;
	} else {
		first_awake_thread = thread->next_awake;
	}

	if (thread == running_thread) {
		ScheduleNextThread();
	}
}


// Schedules a thread if we are currently halted - such as an interrupt
// woke up a thread.
void ScheduleThreadIfWeAreHalted() {
	if (running_thread == NULL && first_awake_thread != NULL) {
		// No thread was running, but there is a thread waiting to run.
		ScheduleNextThread();
	}
}