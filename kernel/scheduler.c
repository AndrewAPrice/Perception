#include "scheduler.h"
#include "thread.h"
#include "process.h"
#include "isr.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

/* linked list of awake threads */
struct Thread *first_awake_thread;
struct Thread *last_awake_thread;

struct Thread *running_thread; /* currently executing thread */

struct isr_regs *idle_regs; /* idle vaue to return when no thread is running */

void init_scheduler() {
	first_awake_thread = 0;
	last_awake_thread = 0;
	running_thread = 0;
	idle_regs = 0;
}

struct isr_regs *schedule_next(struct isr_regs *regs) {
	struct Thread *next;

	if(running_thread) { /* we were executing a thread */
		running_thread->registers = regs;

		next = running_thread->next_awake; /* next awake */
		if(!next)
			next = first_awake_thread;

	} else { /* it was the kernel's idle thread */
		idle_regs = regs;
		next = first_awake_thread; /* next thread is the first awake one */
	}

	if(!next) {
		running_thread = 0;
		return idle_regs; /* no next thread, enter the kernel's idle thread */
	}
/*
	print_string("Registers:\n");
	print_string("r15: "); print_hex(regs->r15);
	print_string(" r14: "); print_hex(regs->r14);
	print_string("\nr13: "); print_hex(regs->r13);
	print_string(" r12: "); print_hex(regs->r12);
	print_string("\nr11: "); print_hex(regs->r11);
	print_string(" r10: "); print_hex(regs->r10);
	print_string("\nr9:  "); print_hex(regs->r9);
	print_string(" r8:  "); print_hex(regs->r8);
	print_string("\nrbp: "); print_hex(regs->rbp);
	print_string(" rdi: "); print_hex(regs->rdi);
	print_string("\nrsi: "); print_hex(regs->rsi);
	print_string(" rdx: "); print_hex(regs->rdx);
	print_string("\nrbx: "); print_hex(regs->rbx);
	print_string(" rax: "); print_hex(regs->rax);
	print_string("\nint: "); print_hex(regs->int_no);
	print_string(" err: "); print_hex(regs->err_code);
	print_string("\nrip: "); print_hex(regs->rip);
	print_string(" cs:  "); print_hex(regs->cs);
	print_string("\nefl: "); print_hex(regs->eflags);
	print_string(" usp: "); print_hex(regs->usersp);
	print_string("\nss: "); print_hex(regs->ss);
	
	asm("hlt");*/

	/* enter the next thread */
	running_thread = next;
	running_thread->time_slices++;

	if(running_thread->process) /* not a kernel thread, make sure we have this process's virtual address space loaded */
		switch_to_address_space(running_thread->process->pml4);

	return running_thread->registers; /* enter into this thread */
}

void schedule_thread(struct Thread *thread) {
	lock_interrupts();
	if(thread->awake)
		return;

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
	unlock_interrupts();
}

void unschedule_thread(struct Thread *thread) {
	lock_interrupts();
	if(!thread->awake)
		return;

	thread->awake = 0;

	if(thread->next_awake)
		thread->next_awake->previous_awake = thread->previous_awake;
	else
		last_awake_thread = thread->previous_awake;

	if(thread->previous_awake)
		thread->previous_awake->next_awake = thread->next_awake;
	else
		first_awake_thread = thread->next_awake;
	unlock_interrupts();
}