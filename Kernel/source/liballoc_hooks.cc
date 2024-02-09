#ifndef __TEST__
#include "physical_allocator.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

extern "C" {

/** This function is supposed to lock the memory data structures. It
 * could be as simple as disabling interrupts or acquiring a spinlock.
 * It's up to you to decide.
 *
 * \return 0 if the lock was acquired successfully. Anything else is
 * failure.
 */
int liballoc_lock() {
  // This might become an issue if we have kernel threads. But for now, all
  // kernel code should be from inside a syscall.
  return 0;
}

/** This function unlocks what was previously locked by the liballoc_lock
 * function.  If it disabled interrupts, it enables interrupts. If it
 * had acquiried a spinlock, it releases the spinlock. etc.
 *
 * \return 0 if the lock was successfully released.
 */
int liballoc_unlock() {
  // This might become an issue if we have kernel threads. But for now, all
  // kernel code should be from inside a syscall.
  return 0;
}

/** This is the hook into the local system which allocates pages. It
 * accepts an integer parameter which is the number of pages
 * required.  The page size was set up in the liballoc_init function.
 *
 * \return nullptr if the pages were not allocated.
 * \return A pointer to the allocated memory.
 */
void* liballoc_alloc(size_t pages) {
  return (void*)AllocateVirtualMemoryInAddressSpace(&kernel_address_space,
                                                    pages);
}

/** This frees previously allocated memory. The void* parameter passed
 * to the function is the exact same value returned from a previous
 * liballoc_alloc call.
 *
 * The integer value is the number of pages to free.
 *
 * \return 0 if the memory was successfully freed.
 */
int liballoc_free(void* addr, size_t pages) {
  ReleaseVirtualMemoryInAddressSpace(&kernel_address_space, (size_t)addr, pages,
                                     true);
  return 0;
}

}  // extern "C"

#endif
