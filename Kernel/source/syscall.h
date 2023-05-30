#pragma once
#include "types.h"

struct isr_regs;

#define NUMBER_OF_SYSCALLS 54

extern void InitializeSystemCalls();

extern char* GetSystemCallName(int syscall);