#pragma once
#include "types.h"

struct gdt_entry {
	unsigned short limit_low;
	unsigned short base_low;
	unsigned char base_middle;
	unsigned char access;
	unsigned char granularity;
	unsigned char base_high;
} __attribute__((packed));

struct gdt_ptr {
	unsigned short limit;
	unsigned int base;
} __attribute__ ((packed));

extern void gdt_flush();
extern void gdt_set_gate(int num, size_t base, size_t limit, unsigned char access, unsigned char gran);
extern void gdt_install();
