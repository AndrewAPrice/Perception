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

#include "interrupts.h"

#include "exceptions.h"
#include "idt.h"
#include "io.h"
#include "physical_allocator.h"
#include "text_terminal.h"
#include "tss.h"
#include "virtual_allocator.h"

extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

// The top of the interrupt's stack.
size_t interrupt_stack_top;

// A list of function pointers to our IRQ handlers.
irq_handler_ptr irq_handlers[16] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

// Remaps hardware interrupts 0->15 to 32->47 on the Interrupt Descriptor Table to not overlap
// with CPU exceptions.
void RemapIrqsToNotOverlapWithCpuExceptions() {
	outportb(0x20, 0x11);
    outportb(0xA0, 0x11);
    outportb(0x21, 0x20);
    outportb(0xA1, 0x28);
    outportb(0x21, 0x04);
    outportb(0xA1, 0x02);
    outportb(0x21, 0x01);
    outportb(0xA1, 0x01);
    outportb(0x21, 0x0);
    outportb(0xA1, 0x0);
}

// Registers the 16 hardware interrupt handlers.
void RegisterInterruptHandlers() {
	RemapIrqsToNotOverlapWithCpuExceptions();

	SetIdtEntry(32, (size_t)irq0, 0x08, 0x8E);
	SetIdtEntry(33, (size_t)irq1, 0x08, 0x8E);
	SetIdtEntry(34, (size_t)irq2, 0x08, 0x8E);
	SetIdtEntry(35, (size_t)irq3, 0x08, 0x8E);
	SetIdtEntry(36, (size_t)irq4, 0x08, 0x8E);
	SetIdtEntry(37, (size_t)irq5, 0x08, 0x8E);
	SetIdtEntry(38, (size_t)irq6, 0x08, 0x8E);
	SetIdtEntry(39, (size_t)irq7, 0x08, 0x8E);
	SetIdtEntry(40, (size_t)irq8, 0x08, 0x8E);
	SetIdtEntry(41, (size_t)irq9, 0x08, 0x8E);
	SetIdtEntry(42, (size_t)irq10, 0x08, 0x8E);
	SetIdtEntry(43, (size_t)irq11, 0x08, 0x8E);
	SetIdtEntry(44, (size_t)irq12, 0x08, 0x8E);
	SetIdtEntry(45, (size_t)irq13, 0x08, 0x8E);
	SetIdtEntry(46, (size_t)irq14, 0x08, 0x8E);
	SetIdtEntry(47, (size_t)irq15, 0x08, 0x8E);
}

// Allocates a stack to use for interrupts.
void AllocateInterruptStack() {
	size_t physical_addr = GetPhysicalPage();
	size_t virtual_addr = FindFreePageRange(kernel_pml4, 1);
	if (physical_addr == OUT_OF_PHYSICAL_PAGES || virtual_addr == OUT_OF_MEMORY) {
		PrintString("Out of memory to allocate interrupt stack.");
		__asm__ __volatile__("cli");
		__asm__ __volatile__("hlt");
	}

	MapPhysicalPageToVirtualPage(kernel_pml4, virtual_addr, physical_addr);

	/*
	PrintString("Interrupt stack is at ");
	PrintHex(virtual_addr);
	PrintChar('\n');
	*/

	interrupt_stack_top = virtual_addr + PAGE_SIZE;

	SetInterruptStack(virtual_addr);
}

// Initializes interrupts.
void InitializeInterrupts() {
	InitializeIdt();
	AllocateInterruptStack();

	// There are two sets of interrupts - CPU exceptions and hardware signals. We'll
	// register handler for both.
	RegisterExceptionInterrupts();
	RegisterInterruptHandlers();
}


// Installs an interrupt handler that gets called when the hardware interrupt occurs.
void InstallHardwareInterruptHandler(int irq, irq_handler_ptr handler) {
	irq_handlers[irq] = handler;
}

// Uninstalls an interrupt handler.
void UninstallHardwareInterruptHandler(int irq) {
	irq_handlers[irq] = 0;
}

extern struct isr_regs *currently_executing_thread_regs;

// The common handler that is called when a hardware interrupt occurs.
void CommonHardwareInterruptHandler(int interrupt_no) {
//	MarkInterruptHandlerAsEntered();

	// See if we have a handler for this interrupt, and dispatch it.
	irq_handler_ptr handle = irq_handlers[interrupt_no];
	if(handle) {
		/*
		PrintString("Hello from interrupt ");
		PrintNumber(interrupt_no);
		PrintChar('\n');
		PrintString("Registers are: SS: ");
		PrintHex(currently_executing_thread_regs->ss);
		PrintString(" User SP: ");
		PrintHex(currently_executing_thread_regs->usersp);
		PrintString(" Eflags: ");
		PrintHex(currently_executing_thread_regs->eflags);
		PrintString(" CS: ");
		PrintHex(currently_executing_thread_regs->cs);
		PrintString(" RIP: ");
		PrintHex(currently_executing_thread_regs->rip);
		PrintString(" RBP: ");
		PrintHex(currently_executing_thread_regs->rbp);
		PrintString(" RAX: ");
		PrintHex(currently_executing_thread_regs->rax);
		PrintString(" RBX: ");
		PrintHex(currently_executing_thread_regs->rbx);
		PrintString(" RCX: ");
		PrintHex(currently_executing_thread_regs->rcx);
		PrintString(" RDX: ");
		PrintHex(currently_executing_thread_regs->rdx);
		PrintString(" RSI: ");
		PrintHex(currently_executing_thread_regs->rsi);
		PrintString(" RDI: ");
		PrintHex(currently_executing_thread_regs->rdi);
		PrintString(" R8: ");
		PrintHex(currently_executing_thread_regs->r8);
		PrintString(" R9: ");
		PrintHex(currently_executing_thread_regs->r9);
		PrintString(" R10: ");
		PrintHex(currently_executing_thread_regs->r10);
		PrintString(" R11: ");
		PrintHex(currently_executing_thread_regs->r11);
		PrintString(" R12: ");
		PrintHex(currently_executing_thread_regs->r12);
		PrintString(" R13: ");
		PrintHex(currently_executing_thread_regs->r13);
		PrintString(" R14: ");
		PrintHex(currently_executing_thread_regs->r14);
		PrintString(" R15: ");
		PrintHex(currently_executing_thread_regs->r15);
		PrintChar('\n');
		*/
		handle();
		/*
		PrintString("New registers are: SS: ");
		PrintHex(currently_executing_thread_regs->ss);
		PrintString(" User SP: ");
		PrintHex(currently_executing_thread_regs->usersp);
		PrintString(" Eflags: ");
		PrintHex(currently_executing_thread_regs->eflags);
		PrintString(" CS: ");
		PrintHex(currently_executing_thread_regs->cs);
		PrintString(" RIP: ");
		PrintHex(currently_executing_thread_regs->rip);
		PrintString(" RBP: ");
		PrintHex(currently_executing_thread_regs->rbp);
		PrintString(" RAX: ");
		PrintHex(currently_executing_thread_regs->rax);
		PrintString(" RBX: ");
		PrintHex(currently_executing_thread_regs->rbx);
		PrintString(" RCX: ");
		PrintHex(currently_executing_thread_regs->rcx);
		PrintString(" RDX: ");
		PrintHex(currently_executing_thread_regs->rdx);
		PrintString(" RSI: ");
		PrintHex(currently_executing_thread_regs->rsi);
		PrintString(" RDI: ");
		PrintHex(currently_executing_thread_regs->rdi);
		PrintString(" R8: ");
		PrintHex(currently_executing_thread_regs->r8);
		PrintString(" R9: ");
		PrintHex(currently_executing_thread_regs->r9);
		PrintString(" R10: ");
		PrintHex(currently_executing_thread_regs->r10);
		PrintString(" R11: ");
		PrintHex(currently_executing_thread_regs->r11);
		PrintString(" R12: ");
		PrintHex(currently_executing_thread_regs->r12);
		PrintString(" R13: ");
		PrintHex(currently_executing_thread_regs->r13);
		PrintString(" R14: ");
		PrintHex(currently_executing_thread_regs->r14);
		PrintString(" R15: ");
		PrintHex(currently_executing_thread_regs->r15);
		*/
	}

	// If the IDTt entry that was invoked was greater than 40 (IRQ 8-15) we need to send an
	// EOI to the slave controller.
	if(interrupt_no >= 8) {
		outportb(0xA0, 0x20);
	}

	// Send an EOI to the master interrupt controllerr.
	outportb(0x20, 0x20);
}

#if 0
void lock_interrupts() {
	if(in_interrupt) return; /* do nothing inside an interrupt because they're already disabled */
	if(interrupt_locks == 0)
		__asm__ __volatile__ ("cli");
	interrupt_locks++;
	//PrintString("+");
}

void unlock_interrupts() {
	if(in_interrupt) return; /* do nothing inside an interrupt because they're already disabled */
	interrupt_locks--;
	if(interrupt_locks == 0)
		__asm__ __volatile__ ("sti");
	// PrintString("|");
}
#endif