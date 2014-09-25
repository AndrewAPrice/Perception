#include "idt.h"
#include "io.h"
#include "physical_allocator.h"
#include "virtual_allocator.h"
#include "text_terminal.h"

struct idt_entry *idt;
struct idt_ptr idt_p;

void idt_set_gate(unsigned char num, size_t base, unsigned short sel, unsigned char flags) {
	/* base address */
	idt[num].base_low = (base & 0xFFFF);
	idt[num].base_middle = (base >> 16) & 0xFFFF;
	idt[num].base_high = (base >> 32);

	idt[num].sel = sel;
	idt[num].always0 = 0;
	idt[num].flags = flags;
}

void idt_install() {
	/* the idt aligns with pages - so grab a page to allocate it */
	idt = (struct idt_entry *)find_free_page_range(kernel_pml4, 1);

	size_t idt_physical = get_physical_page();

	print_string("IDT Address: ");
	print_hex((size_t)idt);
	print_string(" (");
	print_hex(idt_physical);
	print_string(")\n");

	map_physical_page(kernel_pml4, (size_t)idt, idt_physical);

	idt_p.limit = (sizeof (struct idt_entry) * 256) - 1;
	idt_p.base = (unsigned int)(size_t)&idt;

	memset((unsigned char *)idt, 0, sizeof(struct idt_entry) * 256);

	/* add any new ISrs to the IDT here using idt_set_gate */

	/* load the new idt pointer - works in virtual address space */
	__asm__ __volatile__ ("lidt (%0)" : : "b"((size_t)&idt_p));
}
