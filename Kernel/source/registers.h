// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "types.h"

// A thread's registers. The code in syscall.asm, exceptions.asm, and
// interrupts.asm depends on the layout of this struct.
struct Registers {
	size_t r15, r14, r13, r12, r11, r10, r9, r8;
	size_t rsi, rdx, rcx, rbx, rax, rdi, rbp;
	size_t rip, cs, rflags, rsp, ss;
};

// Prints the registers, for debugging.
extern void PrintRegisters(struct Registers* regs);