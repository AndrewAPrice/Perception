#pragma once

struct reg128 {
	size_t low;
	size_t high;
};

typedef size_t reg64;

struct ProcessRegisterState {
	/*reg64 ss, rsp, rflags, cs, rip, ds, es, fs, gs, fs_base, gs_base;
	reg64 rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8, r9, r10, r11, r12, r13, r14, r15;
	reg128 xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15;*/
};

struct Process {
	/* physical address of this process's pml4*/
	size_t pml4;
	/* name of the process */
	char name[256];
	/* linked list of processes */
	Process *next;
	Process *previous;
	/* state of the registers for multitasking */
	ProcessRegisterState *registerState;
	/* save/restore floating point registers? */
	bool saveFloatingPointRegisters;
};
