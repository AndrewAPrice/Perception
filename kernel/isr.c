#include "idt.h"
#include "isr.h"
#include "text_terminal.h"

extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

unsigned char in_interrupt;
int interrupt_locks;

void init_isrs() {
	in_interrupt = 0;
	interrupt_locks = 0;

	idt_set_gate(0, (size_t)isr0, 0x08, 0x8E);
	idt_set_gate(1, (size_t)isr1, 0x08, 0x8E);
	idt_set_gate(2, (size_t)isr2, 0x08, 0x8E);
	idt_set_gate(3, (size_t)isr3, 0x08, 0x8E);
	idt_set_gate(4, (size_t)isr4, 0x08, 0x8E);
	idt_set_gate(5, (size_t)isr5, 0x08, 0x8E);
	idt_set_gate(6, (size_t)isr6, 0x08, 0x8E);
	idt_set_gate(7, (size_t)isr7, 0x08, 0x8E);
	idt_set_gate(8, (size_t)isr8, 0x08, 0x8E);
	idt_set_gate(9, (size_t)isr9, 0x08, 0x8E);
	idt_set_gate(10, (size_t)isr10, 0x08, 0x8E);
	idt_set_gate(11, (size_t)isr11, 0x08, 0x8E);
	idt_set_gate(12, (size_t)isr12, 0x08, 0x8E);
	idt_set_gate(13, (size_t)isr13, 0x08, 0x8E);
	idt_set_gate(14, (size_t)isr14, 0x08, 0x8E);
	idt_set_gate(15, (size_t)isr15, 0x08, 0x8E);
	idt_set_gate(16, (size_t)isr16, 0x08, 0x8E);
	idt_set_gate(17, (size_t)isr17, 0x08, 0x8E);
	idt_set_gate(18, (size_t)isr18, 0x08, 0x8E);
	idt_set_gate(19, (size_t)isr19, 0x08, 0x8E);
	idt_set_gate(20, (size_t)isr20, 0x08, 0x8E);
	idt_set_gate(21, (size_t)isr21, 0x08, 0x8E);
	idt_set_gate(22, (size_t)isr22, 0x08, 0x8E);
	idt_set_gate(23, (size_t)isr23, 0x08, 0x8E);
	idt_set_gate(24, (size_t)isr24, 0x08, 0x8E);
	idt_set_gate(25, (size_t)isr25, 0x08, 0x8E);
	idt_set_gate(26, (size_t)isr26, 0x08, 0x8E);
	idt_set_gate(27, (size_t)isr27, 0x08, 0x8E);
	idt_set_gate(28, (size_t)isr28, 0x08, 0x8E);
	idt_set_gate(29, (size_t)isr29, 0x08, 0x8E);
	idt_set_gate(30, (size_t)isr30, 0x08, 0x8E);
	idt_set_gate(31, (size_t)isr31, 0x08, 0x8E);
}


void enter_interrupt() {
	in_interrupt = 1;
}

void leave_interrupt() {
	in_interrupt = 0;
}


void lock_interrupts() {
	if(in_interrupt) return; /* do nothing inside an interrupt because they're already disabled */
	if(interrupt_locks == 0)
		__asm__ __volatile__ ("cli");
	interrupt_locks++;
	/*print_string("+");*/
}

void unlock_interrupts() {
	if(in_interrupt) return; /* do nothing inside an interrupt because they're already disabled */
	interrupt_locks--;
	if(interrupt_locks == 0)
		__asm__ __volatile__ ("sti");
	/*print_string("|");*/
}

char *exception_messages[] = {
	"Division By Zero", /* 0 */
	"Debug", /* 1 */
	"Non Maskable Interrupt", /* 2 */
	"Breakpoint", /* 3 */
	"Into Detected Overflow", /* 4 */
	"Out of Bounds", /* 5 */
	"Invalid Opcode", /* 6 */
	"No Coprocessor", /* 7 */
	"Double Fault", /* 8 */
	"Coprocessor Segment", /* 9 */
	"Bad TSS", /* 10 */
	"Segment Not Present", /* 11 */
	"Stack Fault", /* 12 */
	"General Protection Fault", /* 13 */
	"Page Fault", /* 14 */
	"Unknown Interrupt", /* 15 */
	"Coprocessor Fault", /* 16 */
	"Alignment Check", /* 17 */
	"Machine Check", /* 18 */
	"Reserved", /* 19 */
	"Reserved", /* 20 */
	"Reserved", /* 21 */
	"Reserved", /* 22 */
	"Reserved", /* 23 */
	"Reserved", /* 24 */
	"Reserved", /* 25 */
	"Reserved", /* 26 */
	"Reserved", /* 27 */
	"Reserved", /* 28 */
	"Reserved", /* 29 */
	"Reserved", /* 30 */
	"Reserved" /* 31 */
};

struct isr_regs *fault_handler(struct isr_regs *r) {
	enter_interrupt();
	if(r->int_no < 32) {
		enter_text_mode();
		print_string("\nException occured: ");
		print_number((size_t)r->int_no);
		print_string(" - ");
		print_string(exception_messages[r->int_no]);
	} else {
		print_string("\nUnknown exception: ");
		print_number((size_t)r->int_no);
	}
	asm("hlt");
	/* todo: terminate this thread */
	leave_interrupt();
	return r;
}
