#pragma once
#include "types.h"

struct idt_entry {
	unsigned short base_low;
	unsigned short sel;
	unsigned char always0;
	unsigned char flags;
	unsigned short base_middle;
	unsigned int base_high;
	unsigned int zero; /* reserved */
} __attribute__((packed));

struct idt_ptr {
	unsigned short limit;
	size_t base;
} __attribute__ ((packed));

extern void idt_load();
extern void idt_set_gate(unsigned char num, size_t base, unsigned short sel, unsigned char flags);
extern void idt_install();
