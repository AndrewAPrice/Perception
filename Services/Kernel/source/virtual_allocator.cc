#include "virtual_allocator.h"

#include "object_pool.h"
#include "physical_allocator.h"
#include "process.h"
#include "shared_memory.h"
#include "text_terminal.h"
#include "virtual_address_space.h"

// Paging structures made at boot time, these can be freed after the virtual
// allocator has been initialized.
#ifdef __TEST__
size_t Pml4[512];
size_t Pdpt[512];
size_t Pd[512];
#else
extern "C" size_t Pml4[];
extern "C" size_t Pdpt[];
extern "C" size_t Pd[];
#endif

namespace {

// The highest user space address in lower half "canonical" 48-bit memory.
constexpr size_t kMaxLowerHalfUserSpaceAddress = 0x00007FFFFFFFFFFF;

// The lowest user space address in higher half "canonical" 48-bit memory.
constexpr size_t kMinHigherHalfUserSpaceAddress = 0xFFFF800000000000;

// Page table entries:

// Pointer to a page table used when temporarily mapping physical memory.
size_t *temp_memory_page_table;
// Start address of what the temporary page table refers to.
size_t temp_memory_start;

// Start of the free memory on boot.
extern size_t bssEnd;

// Statically allocated FreeMemoryRanges added to the object pool so they can be
// allocated before the dynamic memory allocation is set up.
constexpr int kStaticallyAllocatedFreeMemoryRangesCount = 2;
VirtualAddressSpace::FreeMemoryRange statically_allocated_free_memory_ranges
    [kStaticallyAllocatedFreeMemoryRangesCount];

}  // namespace

// The kernel's virtual address space.
VirtualAddressSpace kernel_address_space;

// Initializes the virtual allocator.
void InitializeVirtualAllocator() {
  // Long mode was entered with a temporary setup, now it's time to build a
  // real paging system.

  // Add the statically allocated free memory ranges.
  for (int i = 0; i < kStaticallyAllocatedFreeMemoryRangesCount; i++)
    ObjectPool<VirtualAddressSpace::FreeMemoryRange>::Release(
        &statically_allocated_free_memory_ranges[i]);

  // Allocate a physical page to use as the kernel's PML4 and clear it.
  new (&kernel_address_space) VirtualAddressSpace();
  kernel_address_space.InitializeKernelSpace(
      start_of_free_memory_at_boot, temp_memory_start, temp_memory_page_table);

  // Flush and load the kernel's new and final PML4.
  kernel_address_space.SwitchToAddressSpace();

#ifndef __TEST__
  // Reclaim the PML4, PDPT, PD set up at boot time.
  kernel_address_space.FreePages((size_t)&Pml4 + VIRTUAL_MEMORY_OFFSET, 1);
  kernel_address_space.FreePages((size_t)&Pdpt + VIRTUAL_MEMORY_OFFSET, 1);
  kernel_address_space.FreePages((size_t)&Pd + VIRTUAL_MEMORY_OFFSET, 1);
#endif
}

VirtualAddressSpace &KernelAddressSpace() { return kernel_address_space; }

// Flush the CPU lookup for a particular virtual address.
void FlushVirtualPage(size_t addr) {
#ifndef __TEST__
  __asm__ __volatile__("invlpg (%0)" : : "b"(addr) : "memory");
#endif
}

// Maps a physical page so that we can access it - use this before the virtual
// allocator has been initialized.
void *TemporarilyMapPhysicalMemoryPreVirtualMemory(
    size_t addr, size_t index) {  // Round this down to the nearest 2MB as 2MB
                                  // pages are used before the
  // virtual allocator is set up.
  size_t addr_start = addr & ~(2 * 1024 * 1024 - 1);
  size_t addr_offset = addr - addr_start;
  size_t entry = addr_start | 0x83;

  // The virtual address of the temp page: 1GB - 2MB.
  size_t temp_page_boot = 1022 * 1024 * 1024;
  size_t virtual_address = temp_page_boot + addr_offset;

  // Check if it different to what is currently loaded.
  if (Pd[511] != entry) {
    // Map this to the last page of the page directory set up at boot time.
    Pd[511] = entry;  // Flush the page table cache.
    FlushVirtualPage(addr_start);
  }

  // Return a pointer to the virtual address of the requested physical memory.
  return (void *)(temp_page_boot + addr_offset);
}

// Temporarily maps physical memory (page aligned) into virtual memory so it
// can be fiddled with. index is from 0 to 511 - mapping a different address to
// the same index unmaps the previous page mapped there.
void *TemporarilyMapPhysicalPages(size_t addr, size_t index) {
  size_t entry = addr | 0x3;

  size_t temp_addr = temp_memory_start + PAGE_SIZE * index;

  // Check if it's not already mapped.
  if (temp_memory_page_table[index] != entry) {
    // Map this page into the temporary page table.
    temp_memory_page_table[index] = entry;
    // Flush the page table cache.
    FlushVirtualPage(temp_addr);
  }

  // Return a pointer to the virtual address of the requested physical memory.
  return (void *)temp_addr;
}

// Maps shared memory into a process's virtual address space. Returns nullptr
// if there was an issue.
SharedMemoryInProcess *MapSharedMemoryIntoProcess(Process *process,
                                                  SharedMemory *shared_memory) {
  // Find a free page range to map this shared memory into.
  size_t virtual_address =
      process->virtual_address_space.FindAndReserveFreePageRange(
          shared_memory->size_in_pages);
  if (virtual_address == OUT_OF_MEMORY) {
    // No space to allocate these pages to!
    return nullptr;
  }

  return MapSharedMemoryIntoProcessAtAddress(process, shared_memory,
                                             virtual_address);
}

SharedMemoryInProcess *MapSharedMemoryIntoProcessAtAddress(
    Process *process, SharedMemory *shared_memory, size_t virtual_address) {
  auto shared_memory_in_process = ObjectPool<SharedMemoryInProcess>::Allocate();
  if (shared_memory_in_process == nullptr) {
    // Out of memory.
    process->virtual_address_space.MarkAddressRangeAsFree(
        virtual_address, shared_memory->size_in_pages);
    return nullptr;
  }

  // Increment the references to this shared memory block.
  shared_memory->processes_referencing_this_block++;

  shared_memory_in_process->shared_memory = shared_memory;
  shared_memory_in_process->process = process;
  shared_memory_in_process->virtual_address = virtual_address;
  shared_memory_in_process->references = 1;

  // Add the shared memory to the process's linked list.
  process->joined_shared_memories.AddBack(shared_memory_in_process);

  // Add the process to the shared memory.
  shared_memory->joined_processes.AddBack(shared_memory_in_process);

  bool can_write = CanProcessWriteToSharedMemory(process, shared_memory);

  // Map the physical pages into memory.
  for (size_t page = 0; page < shared_memory->size_in_pages; page++) {
    // Map the physical page to the virtual address.
    if (shared_memory->physical_pages[page] == OUT_OF_PHYSICAL_PAGES) {
      process->virtual_address_space.MapPhysicalPageAt(virtual_address, 0,
                                                       false, false, true);
    } else {
      process->virtual_address_space.MapPhysicalPageAt(
          virtual_address, shared_memory->physical_pages[page], false,
          can_write, false);
    }

    // Iterate to the next page.
    virtual_address += PAGE_SIZE;
  }

  return shared_memory_in_process;
}

// Unmaps shared memory from a process and releases the SharedMemoryInProcess
// object.
void UnmapSharedMemoryFromProcess(
    SharedMemoryInProcess *shared_memory_in_process) {
  // TODO: Wake any threads waiting for this page. (They'll page fault, but what
  // else can be done?)
  auto *process = shared_memory_in_process->process;
  auto *shared_memory = shared_memory_in_process->shared_memory;

  // Unmap the virtual pages.
  process->virtual_address_space.ReleasePages(
      shared_memory_in_process->virtual_address, shared_memory->size_in_pages);

  process->joined_shared_memories.Remove(shared_memory_in_process);
  shared_memory->joined_processes.Remove(shared_memory_in_process);

  // Decrement the references to this shared memory block.
  shared_memory->processes_referencing_this_block--;
  if (shared_memory->processes_referencing_this_block == 0) {
    // There are no more references to this shared memory block, so the memory
    // can be released.
    ReleaseSharedMemoryBlock(shared_memory);
  } else if (process->pid == shared_memory->creator_pid &&
             (shared_memory->flags & SM_LAZILY_ALLOCATED) != 0) {
    // Unmapping lazily allocated shared memory from the creator. Create the
    // pages of any threads that are sleeping because they're waiting for pages
    // to be created.
    // TODO
  }

  ObjectPool<SharedMemoryInProcess>::Release(shared_memory_in_process);
}

void GetUserspaceVirtualMemoryHole(size_t &hole_start_address,
                                   size_t &hole_end_address, bool inclusive) {
  if (inclusive) {
    hole_start_address = kMaxLowerHalfUserSpaceAddress + 1;
    hole_end_address = kMinHigherHalfUserSpaceAddress - 1;
  } else {
    hole_start_address = kMaxLowerHalfUserSpaceAddress;
    hole_end_address = kMinHigherHalfUserSpaceAddress;
  }
}
