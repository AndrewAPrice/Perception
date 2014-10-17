#pragma once
#include "types.h"

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

struct Message;
struct Thread;

struct Process {
	/* general info */
	char name[256]; /* name of the process */
	size_t pid;

	/* memory stuff */
	size_t pml4; /* physical address of this process's pml4*/
	size_t allocated_pages; /* number of allocated pages */

	/* linked list of messages */
	struct Message *next_message; /* start fetching messages here */
	struct Message *last_message; /* add messages here */
	unsigned short messages;
	struct Thread *waiting_thread; /* thread waiting for a message */

	/* threads */
	struct Thread *first_thread;
	unsigned short threads;

	/* linked list of processes */
	struct Process *next;
	struct Process *previous;
};

/* initializes internal structures for tracking processes */
extern void init_processes();
/* creates a process, returns 0 if there was an error */
extern struct Process *create_process();
/* dstroys a process */
extern void destroy_process(struct Process *process);
/* returns a process with that pid, returns 0 if it doesn't exist */
extern struct Process *get_process_from_pid(size_t pid);
