#include "timer.h"
#include "irq.h"
#include "isr.h"
#include "io.h"

volatile size_t timer_ticks;

void timer_phase(size_t hz) {
	size_t divisor = 1193180 / hz;
	outportb(0x43, 0x36);
	outportb(0x40, divisor & 0xFF);
	outportb(0x40, divisor >> 8);
}

void timer_handle(struct isr_regs *r) {
	timer_ticks++;

	/* if(timer_ticks % 18 == 0) {
		print_string("One second has passed\n");
	} */
}

void timer_install() {
	timer_ticks = 0;
	timer_phase(100);
	timer_enable();
}

void timer_enable() {
	irq_install_handler(0, timer_handle);
}

void timer_disable() {
	irq_install_handler(0, 0);
}

void timer_wait(size_t ticks) {
	unsigned long eticks = timer_ticks + ticks;

	while(timer_ticks < eticks) {
		asm("hlt");
	}
}
