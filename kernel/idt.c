#include "idt.h"
#include "io.h"

struct idt_entry idt[256];
struct idt_ptr idtp;

void idt_set_gate(unsigned char num, size_t base, unsigned short sel, unsigned char flags) {
	/* base address */
	idt[num].base_low = (base & 0xFFFF);
	idt[num].base_high = (base >> 16) & 0xFFFF;

	idt[num].sel = sel;
	idt[num].always0 = 0;
	idt[num].flags = flags;
}

void idt_install() {
	idtp.limit = (sizeof (struct idt_entry) * 256) - 1;
	idtp.base = (unsigned int)(size_t)&idt;

	memset((unsigned char *)&idt, 0, sizeof(struct idt_entry) * 256);

	/* add any new ISrs to the IDT here using idt_set_gate */

	idt_load();
}
