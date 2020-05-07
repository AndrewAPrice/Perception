#pragma once

// The virtual allocator manages virtual memory, with variations of the functions of managing
// userland and kernelland memory. Virtual address spaces are identified by the PML4 address
// being passed around. The kernel has a PML4, and each running process will have it's own PML4.

// Some information on different PML levels: http://wiki.osdev.org/Page_Tables 

#include "types.h"

// The offset from physical to virtual memory.
#define VIRTUAL_MEMORY_OFFSET 0xFFFFFFFF80000000 // 0x8000000000 = 256gb

// The address of the kernel's PML4.
extern size_t kernel_pml4;

// The address of the currently loaded PML4.
extern size_t current_pml4;

// Initializes the virtual allocator.
extern void InitializeVirtualAllocator();

// Maps a physical page so that we can access it with before the virtual allocator has been initialized. Returns a pointer to the
// page in virtual memory space. Only one page at a time can be allocated this way.
extern void *TemporarilyMapPhysicalMemoryPreVirtualMemory(size_t addr);

// Temporarily maps physical memory (page aligned) into virtual memory so we can fiddle with it.
// index is from 0 to 511 - mapping a different address to the same index unmaps the previous page mapped there.
extern void *TemporarilyMapPhysicalMemory(size_t addr, size_t index);

// Finds a range of free physical pages in memory - returns the first address or OUT_OF_MEMORY if it can't find a fit.
extern size_t FindFreePageRange(size_t pml4, size_t pages);

// Maps a physical page to a virtual page. Returns if it was successful.
extern bool MapPhysicalPageToVirtualPage(size_t pml4, size_t virtualaddr, size_t physicaladdr);

extern size_t AllocateVirtualMemoryInAddressSpace(size_t pml4, size_t pages);

extern size_t ReleaseVirtualMemoryInAddressSpace(size_t pml4, size_t addr, size_t pages);

// Unmaps a virtual page - free specifies if that page should be returned to the physical memory manager.
extern void UnmapVirtualPage(size_t pml4, size_t virtualaddr, bool free);

// Return the physical address mapped at a virtual address, returning OUT_OF_MEMORY if is not mapped.
extern size_t GetPhysicalAddress(size_t pml4, size_t virtualaddr);

// Gets or creates a virtual page in an address space, returning the physical address or OUT_OF_MEMORY if it fails.
extern size_t GetOrCreateVirtualPage(size_t pml4, size_t virtualaddr);

// Creates a virtual address space, returns the PML4. Returns OUT_OF_MEMORY if it fails.
extern size_t CreateAddressSpace();

// Frees an address space. Everything it finds will be returned to the physical allocator so unmap any shared memory before. Please
// don't pass it the kernel's PML4.
extern void FreeAddressSpace(size_t pml4);

/* Switch to a virtual address space. Remember to call this if allocating or freeing pages to flush the changes! */
extern void SwitchToAddressSpace(size_t pml4);

// Flush the CPU lookup for a particular virtual address.
extern void FlushVirtualPage(size_t addr);