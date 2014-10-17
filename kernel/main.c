#include "atapi.h"
#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "irq.h"
#include "isr.h"
#include "keyboard.h"
#include "messages.h"
#include "multiboot2.h"
#include "pci.h"
#include "physical_allocator.h"
#include "process.h"
#include "text_terminal.h"
#include "timer.h"
#include "vfs.h"
#include "virtual_allocator.h"

void kmain() {
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

	init_processes();
	init_messages();

    init_idt();
    init_isrs();
    // gdt_install();
    init_irq();

	//init_vfs();

    init_timer();
    init_keyboard();

    init_atapi();

    /* scan the pci bus, devices will be initialized as they're discovered */
    init_pci();
/*
	print_string("Total memory: ");
	print_number(total_system_memory / 1024 / 1024);
	
	print_string("MB Free pages: ");
	print_number(free_pages);

	print_string(" (");
	print_number(free_pages * page_size / 1024 / 1024);
	print_string("MB)\n");
*/

	outportb(0x21, 0xfd); /* keyboard only */
	outportb(0xa1, 0xff);
	asm("sti");

	/* load a file from disk? */

	for(;;) {
		asm("hlt");
	}
}

