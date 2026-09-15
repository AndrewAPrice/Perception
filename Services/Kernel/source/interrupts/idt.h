#pragma once
#include "types.h"

// The interrupt descriptor table (IDT) tells the processor where the interrupt
// handlers (interrupt service routines, or ISRs) are located.

namespace interrupts {

// Initalizes the interrupt descriptor table.
void InitializeIdt();

// Loads the IDT on the current CPU core.
void LoadIdt();

// Sets an IDT entry with an optional IST (Interrupt Stack Table) index (1-7, or 0 for none).
void SetIdtEntry(unsigned char num, size_t handler, unsigned short sel,
                 unsigned char flags, unsigned char ist = 0);

}  // namespace interrupts


