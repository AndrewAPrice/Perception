#pragma once

struct isr_regs;
typedef void (*irq_handler_ptr)(struct isr_regs *r);

extern void irq_install_handler(int irq, irq_handler_ptr handler);
extern void irq_uninstall_handler(int irq);
extern void irq_remap();
extern void irq_install();
extern void irq_handler(struct isr_regs *r);