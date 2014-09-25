#include "physical_allocator.h"
#include "virtual_allocator.h"
#include "text_terminal.h"
#include "vfs.h"
#include "gdt.h"
#include "isr.h"
#include "irq.h"
#include "idt.h"
#include "keyboard.h"
#include "timer.h"
#include "multiboot2.h"

extern unsigned int boot_kernel_pml4, boot_kernel_pml3;

void __attribute__((cdecl)) kmain() {
	/* make sure we were booted with a multiboot2 bootloader - we need this because we depend on GRUB for
	providing us with some initialization information*/
	if(MultibootInfo.magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
		enter_text_mode();
		print_string("Not booted with a multiboot2 bootloader!");
		for(;;) __asm__ __volatile__ ("hlt");
	}

	enter_text_mode();
	print_string("Perception kernel...\n");
	init_physical_allocator();
	init_virtual_allocator();

/*    gdt_install();
    idt_install();
    isrs_install();
    irq_install();

	init_vfs();

    timer_install();
    keyboard_install();
*/



	print_string("Total memory: ");
	print_number(total_system_memory / 1024 / 1024);
	
	print_string("MB Free pages: ");
	print_number(free_pages);

	print_string(" (");
	print_number(free_pages * page_size / 1024 / 1024);
	print_string("MB)\n");

	asm("cli");
	for(;;) {
		asm("hlt");
	}
}

