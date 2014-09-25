#include "physical_allocator.h"
#include "virtual_allocator.h"
#include "text_terminal.h"

/** This function is supposed to lock the memory data structures. It
 * could be as simple as disabling interrupts or acquiring a spinlock.
 * It's up to you to decide. 
 *
 * \return 0 if the lock was acquired successfully. Anything else is
 * failure.
 */
int liballoc_lock() {
	asm("cli");
	return 0;
}

/** This function unlocks what was previously locked by the liballoc_lock
 * function.  If it disabled interrupts, it enables interrupts. If it
 * had acquiried a spinlock, it releases the spinlock. etc.
 *
 * \return 0 if the lock was successfully released.
 */
int liballoc_unlock() {
	asm("sti");
	return 0;
}

/** This is the hook into the local system which allocates pages. It
 * accepts an integer parameter which is the number of pages
 * required.  The page size was set up in the liballoc_init function.
 *
 * \return NULL if the pages were not allocated.
 * \return A pointer to the allocated memory.
 */
void* liballoc_alloc(size_t pages) {
	if(pages != 1) {
		enter_text_mode();
		print_string("Something in the kernel tried to allocate ");
		print_number(pages);
		print_string(" pages (");
		print_number((pages * page_size) / 1024);
		print_string(" KB).\n");
		asm("cli");
		asm("hlt");
	}

	size_t page = find_physical_page();
	if(page == 0)
		return 0;

	mark_physical_page(page);
	return (void *)(page + virtual_memory_offset);
}

/** This frees previously allocated memory. The void* parameter passed
 * to the function is the exact same value returned from a previous
 * liballoc_alloc call.
 *
 * The integer value is the number of pages to free.
 *
 * \return 0 if the memory was successfully freed.
 */
int liballoc_free(void *addr, size_t pages) {
	if(pages != 1) {
		enter_text_mode();
		print_string("Something in the kernel tried to free ");
		print_number(pages);
		print_string(" pages (");
		print_number((pages * page_size) / 1024);
		print_string(" KB).\n");
		asm("cli");
		asm("hlt");
	}

	size_t page = (size_t)addr - virtual_memory_offset;
	free_physical_page(page);
	return 0;
}
