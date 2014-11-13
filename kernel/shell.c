#include "shell.h"
#include "physical_allocator.h"
#include "text_terminal.h"
#include "storage_device.h"
#include "syscall.h"

void shell_entry() {
	print_string("Total memory:");
	print_size(total_system_memory);

	size_t free_mem = free_pages * page_size;

	print_string(" Used:");
	print_size(total_system_memory - free_mem);
	
	print_string(" Free:");
	print_size(free_mem);
	print_char('\n');

	while(true) { sleep_thread(); asm("hlt");};
}