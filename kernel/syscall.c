#include "isr.h"
#include "scheduler.h"
#include "syscall.h"
#include "text_terminal.h"
#include "thread.h"

extern void syscall_isr();

void init_syscalls() {
	idt_set_gate(0x80, (size_t)syscall_isr, 0x08, 0x8E);
}


struct isr_regs *syscall_handler(struct isr_regs *r) {
	enter_interrupt();

	switch(r->rax) {
	case 0: { /* terminate this thread */
		struct Thread *t = running_thread;
		unschedule_thread(t);
		r = schedule_next(r);
		destroy_thread(t);
	} break;
	case 1: { /* send this thread to sleep */
		unschedule_thread(running_thread);
		r = schedule_next(r);
	} break;
	case 2: { /* send this thread to sleep if a value is not set */
		size_t *ptr = (size_t *)r->rbx;
		/* check if this value is actually mapped */
		
		if(*ptr == 0) {
			unschedule_thread(running_thread);
			r = schedule_next(r);
		}

	} break;
	default: break; /* unknown system call */
	}

	leave_interrupt();

	return r;
}

/* system calls we can call from kernel threads - must be called within a thread once interrupts are enabled
   and not from an interrupt handler */

/* inline assembly syntax:
	__asm__ ("movl %1, %0\n\t"
      	       "addl %2, %0"
	       : "=r" (sum)			output operands
	       : "r" (operand1), "r" (operand2) input operands
	       : "0"); clobbered
*/

void terminate_thread() {
	__asm__ __volatile__("int $0x80"::"a"(0):);

}

void sleep_thread() {
	__asm__ __volatile__("int $0x80"::"a"(1):"memory");
}

void sleep_if_not_set(size_t *addr) {
	__asm__ __volatile__("int $0x80"::"a"(2),"b"((size_t)addr):"memory");
}