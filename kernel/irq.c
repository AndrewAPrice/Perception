#include "isr.h"
#include "irq.h"
#include "idt.h"
#include "io.h"

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

irq_handler_ptr irq_routines[16] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

void irq_install_handler(int irq, irq_handler_ptr handler) {
	irq_routines[irq] = handler;
}

void irq_uninstall_handler(int irq) {
	irq_routines[irq] = 0;
}

void irq_remap() {
	/* remap irqs 0-15 to idt 32 to 47 to not overlap with cpu exceptions */
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

void init_irq() {
	irq_remap();
	idt_set_gate(32, (size_t)irq0, 0x08, 0x8E);
	idt_set_gate(33, (size_t)irq1, 0x08, 0x8E);
	idt_set_gate(34, (size_t)irq2, 0x08, 0x8E);
	idt_set_gate(35, (size_t)irq3, 0x08, 0x8E);
	idt_set_gate(36, (size_t)irq4, 0x08, 0x8E);
	idt_set_gate(37, (size_t)irq5, 0x08, 0x8E);
	idt_set_gate(38, (size_t)irq6, 0x08, 0x8E);
	idt_set_gate(39, (size_t)irq7, 0x08, 0x8E);
	idt_set_gate(40, (size_t)irq8, 0x08, 0x8E);
	idt_set_gate(41, (size_t)irq9, 0x08, 0x8E);
	idt_set_gate(42, (size_t)irq10, 0x08, 0x8E);
	idt_set_gate(43, (size_t)irq11, 0x08, 0x8E);
	idt_set_gate(44, (size_t)irq12, 0x08, 0x8E);
	idt_set_gate(45, (size_t)irq13, 0x08, 0x8E);
	idt_set_gate(46, (size_t)irq14, 0x08, 0x8E);
	idt_set_gate(47, (size_t)irq15, 0x08, 0x8E);
}

void irq_handler(struct isr_regs *r) {
	irq_handler_ptr handle = irq_routines[r->int_no - 32];
	if(handle)
		handle(r);

	/* if the idt entry that was invoked greater than 40 (irq8-15) we need to send an EOI to the slave controller */
	if(r->int_no >= 40)
		outportb(0xA0, 0x20);

	/* send an EOI to the master interrupt controller */
	outportb(0x20, 0x20);
}
