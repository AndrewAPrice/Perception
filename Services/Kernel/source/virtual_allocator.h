#pragma once

// The virtual allocator manages virtual memory, with variations of the
// functions for managing userland and kernelland memory. Virtual address spaces
// are identified by the PML4 address being passed around. The kernel has a
// PML4, and each running process will have its own PML4.

// Some information on different PML levels: http://wiki.osdev.org/Page_Tables

#include "aa_tree.h"
#include "enum.h"
#include "linked_list.h"
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
  LinkedListNode node;

  // Node in the tree of free address spaces by start address.
  AATreeNode node_by_address;

  // Node in the tree of free address spaces by size.
  AATreeNode node_by_size;
};

// Represents a virtual address space for a process.
struct VirtualAddressSpace {
  // Physical address of the PML4 for this virtual address space.
  size_t pml4;

  // Linked list of free memory ranges.
  LinkedList<FreeMemoryRange, &FreeMemoryRange::node> free_memory_ranges;

  // Tree of free chunks by start address.
  AATree<FreeMemoryRange, &FreeMemoryRange::node_by_address,
         &FreeMemoryRange::start_address>
      free_chunks_by_address;

  // Tree of free chunks by size.
  AATree<FreeMemoryRange, &FreeMemoryRange::node_by_size,
         &FreeMemoryRange::pages>
      free_chunks_by_size;
};

// The kernel's virtual address space.
extern VirtualAddressSpace kernel_address_space;

// The currently loaded virtual address space.
extern VirtualAddressSpace *current_address_space;

// Initializes the virtual allocator.
extern void InitializeVirtualAllocator();

extern bool InitializeVirtualAddressSpace(
    VirtualAddressSpace *virtual_address_space);

// Maps a physical page so that we can access it with before the virtual
// allocator has been initialized. Returns a pointer to the page in virtual
// memory space. Only one page at a time can be allocated this way. The index
// is ignored but is used to match the function definition of
// `TemporarilyMapPhysicalMemory`.
extern void *TemporarilyMapPhysicalMemoryPreVirtualMemory(size_t addr,
                                                          size_t index);

// Temporarily maps physical memory (page aligned) into virtual memory so we can
// fiddle with it. index is from 0 to 511 - mapping a different address to the
// same index unmaps the previous page mapped there.
extern void *TemporarilyMapPhysicalMemory(size_t addr, size_t index);

// Finds a range of free physical pages in memory - returns the first address or
// OUT_OF_MEMORY if it can't find a fit.
extern size_t FindAndReserveFreePageRange(VirtualAddressSpace *address_space,
                                          size_t pages);

// Reserves a range of address page in a virtual address space, only if all the
// pages within this range are currently free.
extern bool ReserveAddressRange(VirtualAddressSpace *address_space,
                                size_t address, size_t pages);

// Maps a physical page to a virtual page. Make sure the address range has
// already been reserved. Returns if it was successful.
extern bool MapPhysicalPageToVirtualPage(VirtualAddressSpace *address_space,
                                         size_t virtualaddr,
                                         size_t physicaladdr, bool own,
                                         bool can_write,
                                         bool throw_exception_on_access);

extern size_t AllocateVirtualMemoryInAddressSpace(
    VirtualAddressSpace *address_space, size_t pages);

extern size_t AllocateVirtualMemoryInAddressSpaceBelowMaxBaseAddress(
    VirtualAddressSpace *address_space, size_t pages, size_t max_base_address);

// Flags for releasing memory.
enum class ReleaseMemoryFlags {
  None = 0,
  // Do not free the underlying memory pages.
  DoNotFreeMemory = 1
};
ENUM_BINARY_OPERATORS(ReleaseMemoryFlags);

// Releases virtual memory in address space.
extern void ReleaseVirtualMemoryInAddressSpace(
    VirtualAddressSpace *address_space, size_t addr, size_t pages,
    ReleaseMemoryFlags flags = ReleaseMemoryFlags::None);

// Maps phyical memory into an address space. Returns the virtual address of
// where it is mapped.
extern size_t MapPhysicalMemoryInAddressSpace(
    VirtualAddressSpace *address_space, size_t addr, size_t pages);

// Marks an address range as being free in the address space, starting at the
// address and spanning the provided number of pages.
extern void MarkAddressRangeAsFree(VirtualAddressSpace *address_space,
                                   size_t address, size_t pages);

// Unmaps a virtual page - free specifies if that page should be returned to the
// physical memory manager.
extern void UnmapVirtualPage(VirtualAddressSpace *address_space,
                             size_t virtualaddr, ReleaseMemoryFlags flags = ReleaseMemoryFlags::None);

// Return the physical address mapped at a virtual address, returning
// OUT_OF_MEMORY if is not mapped.
extern size_t GetPhysicalAddress(VirtualAddressSpace *address_space,
                                 size_t virtualaddr, bool ignore_unowned_pages);

// Gets or creates a virtual page in an address space, returning the physical
// address or OUT_OF_MEMORY if it fails.
extern size_t GetOrCreateVirtualPage(VirtualAddressSpace *address_space,
                                     size_t virtualaddr);

// Frees an address space. Everything it finds will be returned to the physical
// allocator so unmap any shared memory before. Please don't pass it the
// kernel's PML4.
extern void FreeAddressSpace(VirtualAddressSpace *address_space);

// Switch to a virtual address space. Remember to call this if allocating or
// freeing pages to flush the changes!
extern void SwitchToAddressSpace(VirtualAddressSpace *address_space);

// Flush the CPU lookup for a particular virtual address.
extern void FlushVirtualPage(size_t addr);

// Maps shared memory into a process's virtual address space. Returns nullptr if
// there was an issue.
extern SharedMemoryInProcess *MapSharedMemoryIntoProcess(
    Process *process, SharedMemory *shared_memory);

extern void PrintFreeAddressRanges(VirtualAddressSpace &address_space);

// Maps shared memory into a process's virtual address space starting at the
// given virtual address. Returns nullptr if there was an issue. Make sure
// FindAndReserveFreePageRange or ReserveAddressRange was called for the address
// range before calling this.
extern SharedMemoryInProcess *MapSharedMemoryIntoProcessAtAddress(
    Process *process, SharedMemory *shared_memory, size_t virtual_address);

// Unmaps shared memory from a process and releases the SharedMemoryInProcess
// object.
extern void UnmapSharedMemoryFromProcess(
    SharedMemoryInProcess *shared_memory_in_process);

// Sets the access rights of a memory page, if the process owns the page. The
// rights are a bitfield:
//  Bit 0: The memory can be written to.
//  Bit 1: The memory can be executed.
extern void SetMemoryAccessRights(VirtualAddressSpace *address_space,
                                  size_t address, size_t rights);

// Returns whether the provided address lives within kernel space.
extern bool IsKernelAddress(size_t address);

// Returns the non-canonical hole in virtual memory. Start and end addresses are
// inclusive.
extern void GetUserspaceVirtualMemoryHole(size_t &hole_start_address,
                                          size_t &hole_end_address);

// Calculates the number of pages required to fit this particular number of
// bytes.
extern size_t PagesThatContainBytes(size_t bytes);

// Copies data from the module into the process's memory.
extern bool CopyKernelMemoryIntoProcess(size_t from_start, size_t to_start,
                                        size_t to_end, Process *process);
