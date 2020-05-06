#pragma once
#include "types.h"

struct isr_regs {
	size_t r15, r14, r13, r12, r11, r10, r9, r8;
	size_t /*rsp,*/ rbp, rdi, rsi, rdx, rcx, rbx, rax;
	size_t int_no, err_code;
	size_t rip, cs, eflags, usersp, ss;
};

extern void init_isrs();
extern void enter_interrupt();
extern void leave_interrupt();
extern void lock_interrupts();
extern void unlock_interrupts();
