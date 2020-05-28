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

#include "registers.h"

#include "text_terminal.h"

// Prints the registers, for debugging.
void PrintRegisters(struct Registers* regs) {
	PrintString("Printing registers:\n");

	PrintString("rax: "); PrintHex(regs->rax);
	PrintString("   rbx: "); PrintHex(regs->rbx);
	PrintString("\nrcx: "); PrintHex(regs->rcx);
	PrintString("   rdx: "); PrintHex(regs->rdx);
	PrintString("\nrsp: "); PrintHex(regs->rsp);
	PrintString("   rbp: "); PrintHex(regs->rbp);
	PrintString("\nrsi: "); PrintHex(regs->rsi);
	PrintString("   rdi: "); PrintHex(regs->rdi);
	PrintString("\n r8: "); PrintHex(regs->r8);
	PrintString("    r9: "); PrintHex(regs->r9);
	PrintString("\nr10: "); PrintHex(regs->r10);
	PrintString("   r11: "); PrintHex(regs->r11);
	PrintString("\nr12: "); PrintHex(regs->r12);
	PrintString("   r13: "); PrintHex(regs->r13);
	PrintString("\nr14: "); PrintHex(regs->r14);
	PrintString("   r15: "); PrintHex(regs->r15);
	PrintString("\n cs: "); PrintHex(regs->cs);
	PrintString("    ss: "); PrintHex(regs->ss);
	PrintString("\nrip: "); PrintHex(regs->rip);
	PrintString(" flags: "); PrintHex(regs->rflags);
	PrintChar('\n');
}
