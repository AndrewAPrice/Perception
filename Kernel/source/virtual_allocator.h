#pragma once

// The virtual allocator manages virtual memory, with variations of the
// functions for managing userland and kernelland memory. Virtual address spaces
// are identified by the PML4 address being passed around. The kernel has a
// PML4, and each running process will have its own PML4.

// Some information on different PML levels: http://wiki.osdev.org/Page_Tables

#include "aa_tree.h"
#include "types.h"

struct Process;
struct SharedMemoryInProcess;
struct SharedMemory;

// The offset from physical to virtual memory.
#define VIRTUAL_MEMORY_OFFSET 0xFFFFFFFF80000000  // 0x8000000000 = 256gb

// Represents a free range of a virtual address space.
struct FreeMemoryRange {
  // The start address of this free range.
  size_t start_address;

  // The size of this free range, in pages.
  size_t pages;

  // Position in the linked list of free memory ranges.
  struct FreeMemoryRange *previous, *next;

  // Node in the tree of free address spaces by start address.
  struct AATreeNode node_by_address;

  // Node in the tree of free address spaces by size.
  struct AATreeNode node_by_size;
};

// Represents a virtual address space for a process.
struct VirtualAddressSpace {
  // Physical address of the PML4 for this virtual address space.
  size_t pml4;

  // Linked list of free memory ranges.
  struct FreeMemoryRange *free_memory_ranges;

  // Tree of free chunks by start address.
  struct AATree free_chunks_by_address;

  // Tree of free chunks by size.
  struct AATree free_chunks_by_size;
};

// The kernel's virtual address space.
extern struct VirtualAddressSpace kernel_address_space;

// The currently loaded virtual address space.
extern struct VirtualAddressSpace *current_address_space;

// Initializes the virtual allocator.
extern void InitializeVirtualAllocator();

extern bool InitializeVirtualAddressSpace(
    struct VirtualAddressSpace *virtual_address_space);

// Maps a physical page so that we can access it with before the virtual
// allocator has been initialized. Returns a pointer to the page in virtual
// memory space. Only one page at a time can be allocated this way.
extern void *TemporarilyMapPhysicalMemoryPreVirtualMemory(size_t addr);

// Temporarily maps physical memory (page aligned) into virtual memory so we can
// fiddle with it. index is from 0 to 511 - mapping a different address to the
// same index unmaps the previous page mapped there.
extern void *TemporarilyMapPhysicalMemory(size_t addr, size_t index);

// Finds a range of free physical pages in memory - returns the first address or
// OUT_OF_MEMORY if it can't find a fit.
extern size_t FindAndReserveFreePageRange(
    struct VirtualAddressSpace *address_space, size_t pages);

// Maps a physical page to a virtual page. Make sure the address range has
// already been reserved. Returns if it was successful.
extern bool MapPhysicalPageToVirtualPage(
    struct VirtualAddressSpace *address_space, size_t virtualaddr,
    size_t physicaladdr, bool own, bool can_write,
    bool throw_exception_on_access);

extern size_t AllocateVirtualMemoryInAddressSpace(
    struct VirtualAddressSpace *address_space, size_t pages);

extern size_t AllocateVirtualMemoryInAddressSpaceBelowMaxBaseAddress(
    struct VirtualAddressSpace *address_space, size_t pages,
    size_t max_base_address);

extern size_t ReleaseVirtualMemoryInAddressSpace(
    struct VirtualAddressSpace *address_space, size_t addr, size_t pages,
    bool free);

extern size_t MapPhysicalMemoryInAddressSpace(
    struct VirtualAddressSpace *address_space, size_t addr, size_t pages);

// Unmaps a virtual page - free specifies if that page should be returned to the
// physical memory manager.
extern void UnmapVirtualPage(struct VirtualAddressSpace *address_space,
                             size_t virtualaddr, bool free);

// Return the physical address mapped at a virtual address, returning
// OUT_OF_MEMORY if is not mapped.
extern size_t GetPhysicalAddress(struct VirtualAddressSpace *address_space,
                                 size_t virtualaddr, bool ignore_unowned_pages);

// Gets or creates a virtual page in an address space, returning the physical
// address or OUT_OF_MEMORY if it fails.
extern size_t GetOrCreateVirtualPage(struct VirtualAddressSpace *address_space,
                                     size_t virtualaddr);

// Frees an address space. Everything it finds will be returned to the physical
// allocator so unmap any shared memory before. Please don't pass it the
// kernel's PML4.
extern void FreeAddressSpace(struct VirtualAddressSpace *address_space);

// Switch to a virtual address space. Remember to call this if allocating or
// freeing pages to flush the changes!
extern void SwitchToAddressSpace(struct VirtualAddressSpace *address_space);

// Flush the CPU lookup for a particular virtual address.
extern void FlushVirtualPage(size_t addr);

// Maps shared memory into a process's virtual address space. Returns NULL if
// there was an issue.
extern struct SharedMemoryInProcess *MapSharedMemoryIntoProcess(
    struct Process *process, struct SharedMemory *shared_memory);

// Unmaps shared memory from a process and releases the SharedMemoryInProcess
// object.
extern void UnmapSharedMemoryFromProcess(
    struct Process *process,
    struct SharedMemoryInProcess *shared_memory_in_process);

// Sets the access rights of a memory page, if the process owns the page. The
// rights are a bitfield:
//  Bit 0: The memory can be written to.
//  Bit 1: The memory can be executed.
extern void SetMemoryAccessRights(struct VirtualAddressSpace *address_space,
                                  size_t address, size_t rights);
