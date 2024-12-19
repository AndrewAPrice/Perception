#pragma once

// The physical allocator manages physical memory, and operates by grabbing and
// freeing pages (4 KB chunks of memory).

#include "types.h"

// The total number of bytes of system memory.
extern size_t total_system_memory;

// The total number of free pages.
extern size_t free_pages;

// Start of free memory at boot.
extern size_t start_of_free_memory_at_boot;

// The size of a page in bytes. Changing this will probably break the virtual
// allocator.
#define PAGE_SIZE 4096  // 4 KB

// Magic value for when we are out of physical pages.
#define OUT_OF_PHYSICAL_PAGES 1

// Initializes the physical allocator.
void InitializePhysicalAllocator();

// Indicates that we are done with the multiboot memory and that it can be
// released.
void DoneWithMultibootMemory();

// Grabs the next physical page (at boot time before the virtual memory
// allocator is initialized), returns OUT_OF_PHYSICAL_PAGES if there are no more
// physical pages.
size_t GetPhysicalPagePreVirtualMemory();

// Grabs the next physical page, returns OUT_OF_PHYSICAL_PAGES if there are no
// more physical pages.
size_t GetPhysicalPage();

// Grabs the next physical page starting at or bleow the provided physical
// address, returns OUT_OF_PHYSICAL_PAGES if there are no more physical pages.
size_t GetPhysicalPageAtOrBelowAddress(size_t max_base_address);

// Frees a physical page.
void FreePhysicalPage(size_t addr);

// Returns whether an address is the start of a memory page.
bool IsPageAlignedAddress(size_t address);

// Rounds an address down to the start of the page that it's in.
size_t RoundDownToPageAlignedAddress(size_t address);
