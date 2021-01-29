#include "interrupts.h"
#include "io.h"
#include "physical_allocator.h"
#include "process.h"
#include "scheduler.h"
#include "../../third_party/multiboot2.h"
#include "multiboot_modules.h"
#include "syscall.h"
#include "service.h"
#include "text_terminal.h"
#include "thread.h"
#include "timer.h"
#include "tss.h"
#include "virtual_allocator.h"

void kmain() {
	// Make sure we were booted with a multiboot2 bootloader - we need this because we depend
	// on GRUB for providing us with some initialization information.
	if(MultibootInfo.magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
		PrintString("Not booted with a multiboot2 bootloader!");
		for(;;) __asm__ __volatile__ ("hlt");
	}


	InitializePhysicalAllocator();
	InitializeVirtualAllocator();

	InitializeTss();
	InitializeInterrupts();
	InitializeSystemCalls();

	InitializeProcesses();
	InitializeThreads();
	InitializeServices();

	InitializeScheduler();
	InitializeTimer();

	// Loads the multiboot modules, then frees the memory used by them.
	LoadMultibootModules();
	DoneWithMultibootMemory();

	PrintString("Enabling interrupts\n");

	asm("sti");

	for(;;) {
		// This needs to be in a loop because the scheduler returns here when there are no awake threads
		// scheduled.
		asm("hlt");
	}
}
