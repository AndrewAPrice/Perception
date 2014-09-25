#include "gdt.h"

struct gdt_entry gdt[3];
struct gdt_ptr gp;

void gdt_set_gate(int num, size_t base, size_t limit, unsigned char access, unsigned char gran) {
	/* base address */
	gdt[num].base_low = (base & 0xFFFF);
	gdt[num].base_middle = (base >> 16) & 0xFF;
	gdt[num].base_high = (base >> 24) & 0xFF;

	/* descriptor limits */
	gdt[num].limit_low = (limit & 0xFFFF);
	gdt[num].granularity = ((limit >> 16) & 0xF);

	/* granularity and access flags */
	gdt[num].granularity |= (gran & 0xF0);
	gdt[num].access = access;
}

void gdt_install() {
	gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
	gp.base = (unsigned int)(size_t)&gdt;

	/* null descriptor */
	gdt_set_gate(0, 0, 0, 0, 0);

	/* code segment */
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCf);

	/* data segment */
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

	gdt_flush();
}
