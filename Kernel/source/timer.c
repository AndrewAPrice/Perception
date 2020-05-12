#include "timer.h"

#include "interrupts.h"
#include "io.h"
#include "scheduler.h"
#include "text_terminal.h"


// The number of time slices (or how many times the timer triggers) per second.
#define TIME_SLICES_PER_SECOND 100
volatile size_t time_slices;

// Sets the timer to fire 'hz' times per second.
void SetTimerPhase(size_t hz) {
	size_t divisor = 1193180 / hz;
	outportb(0x43, 0x36);
	outportb(0x40, divisor & 0xFF);
	outportb(0x40, divisor >> 8);
}

// The function that gets called each time to timer fires.
void TimerHandler() {
	PrintString("Timer\n");
	time_slices++;

	ScheduleNextThread();
}

// Initializes the timer.
void InitializeTimer() {
	time_slices = 0;
	SetTimerPhase(TIME_SLICES_PER_SECOND);
	InstallHardwareInterruptHandler(0, TimerHandler);
}

/*
void timer_wait(size_t ticks) {
	unsigned long eticks = time_slices + ticks;

	while(time_slices < eticks) {
		asm("hlt");
	}
}*/