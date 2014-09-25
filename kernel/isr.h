#pragma once
#include "types.h"

struct isr_regs {
	size_t gs, fs, es, ds;
	size_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	size_t int_no, err_code;
	size_t eip, cs, eflags, useresp, ss;
};

extern void isrs_install();
extern void fault_handler(struct isr_regs *r);
