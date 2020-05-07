#pragma once
#include "types.h"

// The interrupt descriptor table (IDT) tells the processor where the interrupt handlers (interrupt
// service routines, or ISRs) are located.

// Initalizes the interrupt descriptor table.
extern void InitializeIdt();

// Sets an IDT entry.
extern void SetIdtEntry(
	unsigned char num, size_t handler, unsigned short sel, unsigned char flags);

// Marks the interrupt handler as entered. This is already called for you for interrupt handlers
// registered with InstallHardwareInterruptHandler.
extern void MarkInterruptHandlerAsEntered();

// Marks the interrupt handler as left. This is already called for you for interrupt handlers
// registered with InstallHardwareInterruptHandler.
extern void MarkInterruptHandlerAsLeft();