#pragma once

// The virtual allocator manages virtual memory, with variations of the
// functions for managing userland and kernelland memory. Virtual address spaces
// are identified by the PML4 address being passed around. The kernel has a
// PML4, and each running process will have its own PML4.

// Some information on different PML levels: http://wiki.osdev.org/Page_Tables

#include "types.h"

struct Process;
struct SharedMemoryInProcess;
struct SharedMemory;
class VirtualAddressSpace;

// The offset from physical to virtual memory.
#define VIRTUAL_MEMORY_OFFSET 0xFFFFFFFF80000000  // 0x8000000000 = 256gb

// Initializes the virtual allocator.
void InitializeVirtualAllocator();

// The kernel's virtual address space.
VirtualAddressSpace &KernelAddressSpace();

// Flush the CPU lookup for a particular virtual address.
void FlushVirtualPage(size_t addr);

// Maps a physical page so that we can access it with before the virtual
// allocator has been initialized. Returns a pointer to the page in virtual
// memory space. Only one page at a time can be allocated this way. The index
// is ignored but is used to match the function definition of
// `TemporarilyMapPhysicalMemory`.
void *TemporarilyMapPhysicalMemoryPreVirtualMemory(size_t addr, size_t index);

// Temporarily maps physical memory (page aligned) into virtual memory so we can
// fiddle with it. index is from 0 to 511 - mapping a different address to the
// same index unmaps the previous page mapped there.
void *TemporarilyMapPhysicalPages(size_t addr, size_t index);

// Flush the CPU lookup for a particular virtual address.
void FlushVirtualPage(size_t addr);

// Maps shared memory into a process's virtual address space. Returns nullptr if
// there was an issue.
SharedMemoryInProcess *MapSharedMemoryIntoProcess(Process *process,
                                                  SharedMemory *shared_memory);

// Maps shared memory into a process's virtual address space starting at the
// given virtual address. Returns nullptr if there was an issue. Make sure
// FindAndReserveFreePageRange or ReserveAddressRange was called for the address
// range before calling this.
SharedMemoryInProcess *MapSharedMemoryIntoProcessAtAddress(
    Process *process, SharedMemory *shared_memory, size_t virtual_address);

// Unmaps shared memory from a process and releases the SharedMemoryInProcess
// object.
void UnmapSharedMemoryFromProcess(
    SharedMemoryInProcess *shared_memory_in_process);

// Returns the non-canonical hole in virtual memory.
void GetUserspaceVirtualMemoryHole(size_t &hole_start_address,
                                   size_t &hole_end_address, bool inclusive);
