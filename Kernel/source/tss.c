// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tss.h"

#include "io.h"
#include "liballoc.h"
#include "physical_allocator.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

// Pointers for the TSS entry in the GDT. WARNING: These refer to symbols in
// lower memory, so we'll have to add VIRTUAL_MEMORY_OFFSET when deferencing them.
extern uint64 TSSEntry;

// Size of the TSS 
#define TSS_SIZE 104
// Index of RSP0 in the TSS (stack pointer for ring 0).
#define RSP0_LOW 1
#define RSP0_HIGH 2
// Pointer to the TSS.
uint32* tss;

// TSS offset in the GDT. The GDT is hardcoded in boot.asm.
#define TSS_GDT_OFFSET 0x28

// Initializes the task segment structure.
void InitializeTss() {
	// Allocate and clear the TSS.
	tss = malloc(TSS_SIZE);
	memset((char *)tss, 0, TSS_SIZE);

	// Set the TSS entry in the GDT.
	uint64 tss_entry_low = 0;
	unsigned char* tss_entry_bytes = (unsigned char*)&tss_entry_low;

	size_t limit = TSS_SIZE;
	size_t base = (size_t)tss;

	tss_entry_bytes[0] = limit & 0xFF;
	tss_entry_bytes[1] = (limit >> 8) & 0xFF;
	tss_entry_bytes[6] = /*0x40 |*/ ((limit >> 16) & 0xF);

	tss_entry_bytes[2] = base & 0xFF;
	tss_entry_bytes[3] = (base >> 8) & 0xFF;
	tss_entry_bytes[4] = (base >> 16) & 0xFF;
	tss_entry_bytes[7] = (base >> 24) & 0xFF;

	tss_entry_bytes[5] = 0x89; // Present, TSS

	uint64 tss_entry_high = 0;
	uint32* tss_entry_dwords = (uint32*)&tss_entry_high;

	tss_entry_dwords[0] = (base >> 32) & 0xFFFFFFFF;

	*(uint64*)(((size_t)&TSSEntry) + VIRTUAL_MEMORY_OFFSET) = tss_entry_low;
	*(uint64*)(((size_t)&TSSEntry) + VIRTUAL_MEMORY_OFFSET + 8) = tss_entry_high;

	// Point the IOPB bitmap offset to the end of TSS structure, because it's unused.
	((uint16*)tss)[51] = TSS_SIZE;
}


// Sets the stack to use for interrupts.
void SetInterruptStack(size_t interrupt_stack_start_virtual_addr) {
	// Stacks grow downwards.
	size_t top_of_stack = interrupt_stack_start_virtual_addr + PAGE_SIZE;

	uint32 low = top_of_stack & 0xFFFFFFFF;
	uint32 high = top_of_stack >> 32;

	tss[RSP0_LOW] = low;
	tss[RSP0_HIGH] = high;

	// Load the TSS.
	__asm__ __volatile__("ltr %0":: "r"((uint16)TSS_GDT_OFFSET));
}