#include "scheduler.h"

#include "interrupts.h"
#include "liballoc.h"
#include "thread.h"
#include "process.h"
#include "interrupts.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

//extern void save_fpu_registers(size_t regs_addr);
//extern void load_fpu_registers(size_t regs_addr);

/* linked list of awake threads */
struct Thread *first_awake_thread;
struct Thread *last_awake_thread;

struct Thread *running_thread; /* currently executing thread */

struct isr_regs *idle_regs; /* idle vaue to return when no thread is running */

// Currently executing registers.
struct isr_regs *currently_executing_thread_regs;

void InitializeScheduler() {
	first_awake_thread = 0;
	last_awake_thread = 0;
	running_thread = 0;
	currently_executing_thread_regs = malloc(sizeof(struct isr_regs));
	if (!currently_executing_thread_regs) {
		PrintString("Could not allocate object to store the kernel's registers.");
		__asm__ __volatile__("cli");
		__asm__ __volatile__("hlt");
	}
	idle_regs = currently_executing_thread_regs;

}
//char fxsave_region[512] __attribute__((aligned(16)));

char *fpu_registers_ptr; /* needs to be a constant in memory */

void ScheduleNextThread() {
	// The next thread to switch to.
	struct Thread *next;

	if(running_thread) {
		// We were currently executing a thread.

		asm volatile("fxsave %0"::"m"(*running_thread->fpu_registers));

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
		return;
	}

	/* enter the next thread */
	running_thread = next;
	running_thread->time_slices++;

	/*
	PrintString("Entering thread: ");
	PrintNumber(running_thread->id);
	PrintChar('\n');
	*/

	SwitchToAddressSpace(running_thread->pml4);

	asm volatile("fxrstor %0"::"m"(*running_thread->fpu_registers));

	currently_executing_thread_regs = running_thread->registers;

/*
	PrintString("Registers:\n");
	PrintString("r15: "); PrintHex(currently_executing_thread_regs->r15);
	PrintString(" r14: "); PrintHex(currently_executing_thread_regs->r14);
	PrintString("\nr13: "); PrintHex(currently_executing_thread_regs->r13);
	PrintString(" r12: "); PrintHex(currently_executing_thread_regs->r12);
	PrintString("\nr11: "); PrintHex(currently_executing_thread_regs->r11);
	PrintString(" r10: "); PrintHex(currently_executing_thread_regs->r10);
	PrintString("\nr9:  "); PrintHex(currently_executing_thread_regs->r9);
	PrintString(" r8:  "); PrintHex(currently_executing_thread_regs->r8);
	PrintString("\nrbp: "); PrintHex(currently_executing_thread_regs->rbp);
	PrintString(" rdi: "); PrintHex(currently_executing_thread_regs->rdi);
	PrintString("\nrsi: "); PrintHex(currently_executing_thread_regs->rsi);
	PrintString(" rdx: "); PrintHex(currently_executing_thread_regs->rdx);
	PrintString("\nrbx: "); PrintHex(currently_executing_thread_regs->rbx);
	PrintString(" rax: "); PrintHex(currently_executing_thread_regs->rax);
	PrintString("\nrip: "); PrintHex(currently_executing_thread_regs->rip);
	PrintString(" cs:  "); PrintHex(currently_executing_thread_regs->cs);
	PrintString("\nefl: "); PrintHex(currently_executing_thread_regs->eflags);
	PrintString(" usp: "); PrintHex(currently_executing_thread_regs->usersp);
	PrintString("\nss: "); PrintHex(currently_executing_thread_regs->ss);
*/
}

void ScheduleThread(struct Thread *thread) {
	//lock_interrupts();
	if(thread->awake) {
	//	unlock_interrupts();
		return;
	}

	thread->awake = 1;

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

	thread->awake = 0;

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
