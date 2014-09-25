#pragma once
/* finds and frees physical pages, internally it uses a bitmap */

#include "types.h"

/* the total number of bytes of system memory */
extern size_t total_system_memory;
/* the total number of free bytes of system memory */
extern size_t free_pages;

/* the size of a page in bytes */
extern const size_t page_size;

/* initialises the physical allocator */
extern void init_physical_allocator();
/* grabs the next physical page (at boot time before the virtual memory allocator is initialized), returns 0 if there are no more physical pages */
extern size_t get_physical_page_boot();
/* grabs the next physical page, returns 0 if there are no more physical pages */
extern size_t get_physical_page();
/* frees a physical page */
extern void free_physical_page(size_t addr);
