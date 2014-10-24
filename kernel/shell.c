#include "shell.h"
#include "physical_allocator.h"
#include "text_terminal.h"
#include "syscall.h"

void shell_entry() {
	print_string("Total memory: ");
	print_number(total_system_memory / 1024 / 1024);
	
	print_string("MB Free pages: ");
	print_number(free_pages);

	print_string(" (");
	print_number(free_pages * page_size / 1024 / 1024);
	print_string("MB)\n");

	sleep_thread();

	while(true) {asm("hlt");};
}