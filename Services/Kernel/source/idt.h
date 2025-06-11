#pragma once
#include "types.h"

// The interrupt descriptor table (IDT) tells the processor where the interrupt
// handlers (interrupt service routines, or ISRs) are located.

// Initalizes the interrupt descriptor table.
void InitializeIdt();

// Sets an IDT entry.
void SetIdtEntry(unsigned char num, size_t handler, unsigned short sel,
                 unsigned char flags);
