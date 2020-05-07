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

// The running process's registers at the time the interrupt was called.
// If this struct is changed, we need to update the contents of interrupts.asm
// and exceptions.asm.
struct isr_regs {
	size_t r15, r14, r13, r12, r11, r10, r9, r8;
	size_t rsi, rdx, rcx, rbx, rax, rdi, rbp;
	size_t rip, cs, eflags, usersp, ss;
//	size_t int_no, err_code;
};

// Interrupt handling function.
typedef void (*irq_handler_ptr)();

// Initializes interrupts.
extern void InitializeInterrupts();

// Installs an interrupt handler that gets called when the hardware interrupt occurs.
extern void InstallHardwareInterruptHandler(int irq, irq_handler_ptr handler);

// Uninstalls an interrupt handler.
extern void UninstallHardwareInterruptHandler(int irq);
