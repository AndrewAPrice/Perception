#pragma once
#include "types.h"

#define NUMBER_OF_SYSCALLS 55

extern void InitializeSystemCalls();

extern const char* GetSystemCallName(int syscall);