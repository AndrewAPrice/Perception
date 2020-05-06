#include "timer.h"
#include "irq.h"
#include "isr.h"
#include "io.h"
#include "scheduler.h"

volatile size_t time_slices;

void timer_phase(size_t hz) {
	size_t divisor = 1193180 / hz;
	outportb(0x43, 0x36);
	outportb(0x40, divisor & 0xFF);
	outportb(0x40, divisor >> 8);
}

struct isr_regs *timer_handle(struct isr_regs *r) {
	time_slices++;

	return schedule_next(r);
}

void init_timer() {
	time_slices = 0;
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
	unsigned long eticks = time_slices + ticks;

	while(time_slices < eticks) {
		asm("hlt");
	}
}
