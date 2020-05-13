#pragma once
#include "types.h"

// The programmable interrupt timer (PIT) triggers many times a second and is the basis of
// preemptive multitasking.

// The function that gets called each time to timer fires.
extern void TimerHandler();

// Initializes the timer.
extern void InitializeTimer();