#pragma once
#include "types.h"

// The programmable interrupt timer (PIT) triggers many times a second and is the basis of
// preemptive multitasking.

// Initializes the timer.
extern void InitializeTimer();