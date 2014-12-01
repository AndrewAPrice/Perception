#include "fs.h"
#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "irq.h"
#include "isr.h"
#include "keyboard.h"
#include "messages.h"
#include "mouse.h"
#include "multiboot2.h"
#include "pci.h"
#include "physical_allocator.h"
#include "process.h"
#include "scheduler.h"
#include "shell.h"
#include "storage_device.h"
#include "syscall.h"
#include "text_terminal.h"
#include "thread.h"
#include "timer.h"
#include "vfs.h"
#include "video.h"
#include "virtual_allocator.h"
#include "window_manager.h"

void kmain() {
	/* make sure we were booted with a multiboot2 bootloader - we need this because we depend on GRUB for
	providing us with some initialization information*/
	if(MultibootInfo.magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
		enter_text_mode();
		print_string("Not booted with a multiboot2 bootloader!");
		for(;;) __asm__ __volatile__ ("hlt");
	}

	enter_text_mode();

	enter_interrupt(); /* pretend we're in an interrupt so sti isn't enabled */

	init_physical_allocator();
	init_virtual_allocator();

	init_processes();
	init_messages();

	init_idt();
	init_isrs();
	// gdt_install();
	init_irq();

	init_threads();
	init_scheduler();

	init_timer();
	init_keyboard();
	init_mouse();
	init_fs();
	init_storage_devices();
	init_vfs();
	init_video();

	/* scan the pci bus, devices will be initialized as they're discovered */
	init_pci();

	init_syscalls();

	check_for_video(); /* makes sure we have video */
	
	window_manager_init();

	enter_text_mode();
	print_string("Welcome to Perception...\n");


	/* Create the shell thread */
	schedule_thread(create_thread(0, (size_t)shell_entry, 0));
	/* enable interrupts, this will enter us into our threads */
#if 0
	outportb(0x21, 0xfd); /* keyboard only */
	outportb(0xa1, 0xff);
#endif
	asm("sti");

	for(;;) {
		asm("hlt");
	}
}

