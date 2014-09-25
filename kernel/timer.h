#pragma once
#include "types.h"

extern void timer_phase(size_t hz);
extern void timer_install();
extern void timer_enable();
extern void timer_disable();
extern void timer_wait(size_t ticks);
