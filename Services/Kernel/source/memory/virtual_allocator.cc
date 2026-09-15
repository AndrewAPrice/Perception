#include "memory/virtual_allocator.h"

#include "containers/object_pool.h"
#include "containers/spinlock.h"
#include "scheduling/cpu_core.h"
#include "memory/physical_allocator.h"
#include "processes/process.h"
#include "ipc/shared_memory.h"
#include "output/text_terminal.h"
#include "memory/virtual_address_space.h"

// Paging structures made at boot time, these can be freed after the virtual
// allocator has been initialized.
#ifdef TEST
size_t Pml4[512];
size_t Pdpt[512];
size_t Pd[512];
#else
extern "C" size_t Pml4[];
extern "C" size_t Pdpt[];
extern "C" size_t Pd[];
#endif

namespace memory {

using ::ipc::CanProcessWriteToSharedMemory;
using ::ipc::GetSharedMemoryLock;
using ::ipc::kSmLazilyAllocated;
using ::ipc::ReleaseSharedMemoryBlock;
using ::ipc::SharedMemory;
using ::ipc::SharedMemoryInProcess;
using containers::ObjectPool;
using containers::RecursiveInterruptSafeSpinlockGuard;
using processes::Process;
using scheduling::GetCurrentCpuCore;
using scheduling::kTempSlotsPerCore;

namespace {

// The highest user space address in lower half "canonical" 48-bit memory.
constexpr size_t kMaxLowerHalfUserSpaceAddress = 0x00007FFFFFFFFFFF;

// The lowest user space address in higher half "canonical" 48-bit memory.
constexpr size_t kMinHigherHalfUserSpaceAddress = 0xFFFF800000000000;

// Page table entries:

// Pointer to a page table used when temporarily mapping physical memory.
size_t* g_temp_memory_page_table;
// Start address of what the temporary page table refers to.
size_t g_temp_memory_start;

// Start of the free memory on boot.
extern size_t bssEnd;

// Statically allocated FreeMemoryRanges added to the object pool so they can be
// allocated before the dynamic memory allocation is set up.
constexpr int kStaticallyAllocatedFreeMemoryRangesCount = 2;
VirtualAddressSpace::FreeMemoryRange statically_allocated_free_memory_ranges
    [kStaticallyAllocatedFreeMemoryRangesCount];

// The kernel's virtual address space.
VirtualAddressSpace g_kernel_address_space;

}  // namespace

// Initializes the virtual allocator.
void InitializeVirtualAllocator() {
  // Long mode was entered with a temporary setup, now it's time to build a
  // real paging system.

  // Add the statically allocated free memory ranges.
  for (int i = 0; i < kStaticallyAllocatedFreeMemoryRangesCount; i++) {
    statically_allocated_free_memory_ranges[i].is_static = true;
    ObjectPool<VirtualAddressSpace::FreeMemoryRange>::Release(
        &statically_allocated_free_memory_ranges[i]);
  }

  // Allocate a physical page to use as the kernel's PML4 and clear it.
  new (&g_kernel_address_space) VirtualAddressSpace();
  g_kernel_address_space.InitializeKernelSpace(g_start_of_free_memory_at_boot,
                                               g_temp_memory_start,
                                               g_temp_memory_page_table);

  // Flush and load the kernel's new and final PML4.
  g_kernel_address_space.SwitchToAddressSpace();

#ifndef TEST
  // Boot page tables (Pml4, Pdpt, Pd) are retained for the AP bootstrap
  // trampoline.
#endif
}

VirtualAddressSpace& KernelAddressSpace() { return g_kernel_address_space; }

// Flush the CPU lookup for a particular virtual address.
void FlushVirtualPage(size_t addr) {
#ifndef TEST
  __asm__ __volatile__("invlpg (%0)" : : "b"(addr) : "memory");
#endif
}

#ifndef TEST
void* TemporarilyMapPhysicalMemoryPreVirtualMemory(
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
  volatile size_t* volatile_pd = (volatile size_t*)Pd;
  if (volatile_pd[511] != entry) {
    // Map this to the last page of the page directory set up at boot time.
    volatile_pd[511] = entry;  // Flush the page table cache.
    FlushVirtualPage(temp_page_boot);
  }
  __asm__ __volatile__("" : : : "memory");

  // Return a pointer to the virtual address of the requested physical memory.
  return (void*)(temp_page_boot + addr_offset);
}

void* TemporarilyMapPhysicalPages(size_t addr, size_t index) {
  size_t core_id = 0;
#ifndef TEST
  core_id = GetCurrentCpuCore().core_id;
#endif
  size_t global_index =
      (core_id * kTempSlotsPerCore) + (index % kTempSlotsPerCore);
  size_t entry = addr | 0x3;

  size_t temp_addr = g_temp_memory_start + kPageSize * global_index;

  // Check if it's not already mapped.
  volatile size_t* volatile_table = (volatile size_t*)g_temp_memory_page_table;
  if (volatile_table[global_index] != entry) {
    // Map this page into the temporary page table.
    volatile_table[global_index] = entry;
    // Flush the page table cache.
    FlushVirtualPage(temp_addr);
  }
  __asm__ __volatile__("" : : : "memory");

  // Return a pointer to the virtual address of the requested physical memory.
  return (void*)temp_addr;
}
#endif

SharedMemoryInProcess* MapSharedMemoryIntoProcess(Process* process,
                                                  SharedMemory* shared_memory) {
  RecursiveInterruptSafeSpinlockGuard guard(GetSharedMemoryLock());
  // Find a free page range to map this shared memory into.
  size_t virtual_address =
      process->virtual_address_space.FindAndReserveFreePageRange(
          shared_memory->size_in_pages);
  if (virtual_address == kOutOfMemory) {
    // No space to allocate these pages to!
    return nullptr;
  }

  return MapSharedMemoryIntoProcessAtAddress(process, shared_memory,
                                             virtual_address);
}

SharedMemoryInProcess* MapSharedMemoryIntoProcessAtAddress(
    Process* process, SharedMemory* shared_memory, size_t virtual_address) {
  RecursiveInterruptSafeSpinlockGuard guard(GetSharedMemoryLock());
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
  shared_memory_in_process->mapped_pages = shared_memory->size_in_pages;

  // Add the shared memory to the process's tree.
  process->joined_shared_memories.Insert(shared_memory_in_process);

  // Add the process to the shared memory.
  shared_memory->joined_processes.AddBack(shared_memory_in_process);

  bool can_write = CanProcessWriteToSharedMemory(process, shared_memory);

  // Map the physical pages into memory.
  for (size_t page = 0; page < shared_memory->size_in_pages; page++) {
    // Map the physical page to the virtual address.
    if (shared_memory->physical_pages[page] == kOutOfPhysicalPages) {
      process->virtual_address_space.MapPhysicalPageAt(virtual_address, 0,
                                                       false, false, true);
    } else {
      process->virtual_address_space.MapPhysicalPageAt(
          virtual_address, shared_memory->physical_pages[page], false,
          can_write, false);
    }

    // Iterate to the next page.
    virtual_address += kPageSize;
  }

  return shared_memory_in_process;
}

void UnmapSharedMemoryFromProcess(
    SharedMemoryInProcess* shared_memory_in_process) {
  RecursiveInterruptSafeSpinlockGuard guard(GetSharedMemoryLock());
  // TODO: Wake any threads waiting for this page. (They'll page fault, but what
  // else can be done?)
  auto* process = shared_memory_in_process->process;
  auto* shared_memory = shared_memory_in_process->shared_memory;

  // Unmap the virtual pages.
  process->virtual_address_space.ReleasePages(
      shared_memory_in_process->virtual_address,
      shared_memory_in_process->mapped_pages);

  process->joined_shared_memories.Remove(shared_memory_in_process);
  shared_memory->joined_processes.Remove(shared_memory_in_process);

  // Decrement the references to this shared memory block.
  shared_memory->processes_referencing_this_block--;
  if (shared_memory->processes_referencing_this_block == 0) {
    // There are no more references to this shared memory block, so the memory
    // can be released.
    ReleaseSharedMemoryBlock(shared_memory);
  } else if (process->pid == shared_memory->creator_pid &&
             (shared_memory->flags & kSmLazilyAllocated) != 0) {
    // Unmapping lazily allocated shared memory from the creator. Create the
    // pages of any threads that are sleeping because they're waiting for pages
    // to be created.
    // TODO
  }

  ObjectPool<SharedMemoryInProcess>::Release(shared_memory_in_process);
}

void GetUserspaceVirtualMemoryHole(size_t& hole_start_address,
                                   size_t& hole_end_address, bool inclusive) {
  if (inclusive) {
    hole_start_address = kMaxLowerHalfUserSpaceAddress + 1;
    hole_end_address = kMinHigherHalfUserSpaceAddress - 1;
  } else {
    hole_start_address = kMaxLowerHalfUserSpaceAddress;
    hole_end_address = kMinHigherHalfUserSpaceAddress;
  }
}

}  // namespace memory

