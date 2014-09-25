#pragma once
/* finds and frees virtual pages */
#include "types.h"

/* some info on different pml levels: http://wiki.osdev.org/Page_Tables */

/* offset from physical to virtual memory */
/* offset of kernel memory 0x8000000000 = 256gb */
#define virtual_memory_offset 0xFFFFFFFF80000000

/* map a physical page so that we can access it with temp_page_boot - use this before the virtual allocator has been initialized */
extern void *map_temp_boot_page(size_t addr);

/* KERNEL MEMORY */
/* Initialies the virtual allocator */
extern void init_virtual_allocator();

/* maps a physical page (page aligned) into virtual memory so we can fiddle with it,
  index is from 0 to 511 - mapping a different address to the same index
  unmaps the previous page */
extern void *map_physical_memory(size_t addr, size_t index);

/* Creates a process's virtual address space. process_virtual_pml4 is 0 if it fails. */
extern bool create_process_address_space(size_t *process_pml4);

/* Frees a process's virtual address space. */
extern void free_process_address_space(size_t pml4);

/* Finds a range of free process virtual pages. Returns 0 if it can't fit it.  Doesn't allocate it. */
extern size_t find_free_process_page_range(size_t pml4, size_t pages);

/* Frees a process virtual page. */
extern void free_process_page(size_t pml4, size_t addr);

/* Assignes a process page to a physical page. */
extern bool assign_process_page_to_physical_page(size_t pml4, size_t physical_page, size_t virtual_page);

/* Switch to a virtual address space. Remember to call this if allocating or freeing pages to flush the changes! */
extern void switch_to_address_space(size_t pml4);

/* Flush the CPU lookup for a particular virtual address */
extern void flush_virtual_page(size_t addr);