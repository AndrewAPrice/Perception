#pragma once
#include "types.h"

struct isr_regs;

extern void init_syscalls();
/* system calls we can call from kernel threads - must be called within a thread once interrupts are enabled
   and not from an interrupt handler */

/* terminate this thread */
extern void terminate_thread();
/* send this thread to sleep */
extern void sleep_thread();
/* send this thread to sleep if the value at this address is not set */
extern void sleep_if_not_set(size_t *addr);