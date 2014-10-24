#include "physical_allocator.h"
#include "virtual_allocator.h"
#include "text_terminal.h"
#include "isr.h"

/** This function is supposed to lock the memory data structures. It
 * could be as simple as disabling interrupts or acquiring a spinlock.
 * It's up to you to decide. 
 *
 * \return 0 if the lock was acquired successfully. Anything else is
 * failure.
 */
int liballoc_lock() {
	lock_interrupts();
	return 0;
}

/** This function unlocks what was previously locked by the liballoc_lock
 * function.  If it disabled interrupts, it enables interrupts. If it
 * had acquiried a spinlock, it releases the spinlock. etc.
 *
 * \return 0 if the lock was successfully released.
 */
int liballoc_unlock() {
	unlock_interrupts();
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
	/* find a free page range */
	size_t start = find_free_page_range(kernel_pml4, pages);
	if(start == 0) return 0; /* no free page range */

	/* allocate each one */
	size_t addr = start;
	size_t i;
	for(i = 0; i < pages; i++, addr += page_size) {
		/* get a physical page */
		size_t phys = get_physical_page();

		if(phys == 0) { /* no free physical memory */
			/* unmap all memory up until this point*/
			for(;start < addr; start += page_size)
				unmap_physical_page(kernel_pml4, start, true);

			return 0;
		}

		/* map the physical page */
		map_physical_page(kernel_pml4, addr, phys);

		flush_virtual_page(addr);
	}


	return (void *)start;
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
	size_t i = 0;
	size_t vir_addr = (size_t)addr;
	for(;i < pages; i++, vir_addr += page_size)
		unmap_physical_page(kernel_pml4, vir_addr, true);
	
	return 0;
}
