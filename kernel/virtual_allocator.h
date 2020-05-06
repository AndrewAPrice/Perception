#pragma once
/* finds and frees virtual pages */
#include "types.h"

/* some info on different pml levels: http://wiki.osdev.org/Page_Tables */

/* offset from physical to virtual memory */
/* offset of kernel memory 0x8000000000 = 256gb */
#define virtual_memory_offset 0xFFFFFFFF80000000

/* the kernel's pml4 */
extern size_t kernel_pml4;

/* the currently loaded pml4 */
extern size_t current_pml4;

/* map a physical page so that we can access it with temp_page_boot - use this before the virtual allocator has been initialized */
extern void *map_temp_boot_page(size_t addr);

/* KERNEL MEMORY */
/* Initialies the virtual allocator */
extern void init_virtual_allocator();

/* maps a physical page (page aligned) into virtual memory so we can fiddle with it,
  index is from 0 to 511 - mapping a different address to the same index
  unmaps the previous page */
extern void *map_physical_memory(size_t addr, size_t index);

/* find a range of free physical pages in kernel memory - returns the first addres or 0 if it can't find a fit */
extern size_t find_free_page_range(size_t pml4, size_t pages);

/* maps a physical page to a virtual page */
extern bool map_physical_page(size_t pml4, size_t virtualaddr, size_t physicaladdr);

/* unmaps a physical page - free specifies if that page should be returned to the physical memory manager */
extern void unmap_physical_page(size_t pml4, size_t virtualaddr, bool free);

/* Creates a process's virtual address space, returns the pml4 - pml4 is 0 if it fails. */
extern size_t create_process_address_space();

/* Frees a process's virtual address space. Everything it finds will be returned to the physical allocator so unmap any shared memory before. */
extern void free_process_address_space(size_t pml4);

/* Switch to a virtual address space. Remember to call this if allocating or freeing pages to flush the changes! */
extern void switch_to_address_space(size_t pml4);

/* Flush the CPU lookup for a particular virtual address */
extern void flush_virtual_page(size_t addr);