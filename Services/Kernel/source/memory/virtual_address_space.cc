// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "memory/virtual_address_space.h"

#include "memory/memory.h"
#include "containers/object_pool.h"
#include "containers/spinlock.h"
#include "memory/physical_allocator.h"
#include "processes/process.h"
#include "scheduling/cpu_core.h"
#include "scheduling/scheduler.h"
#include "hardware/smp.h"
#include "hardware/tlb_shootdown.h"
#include "output/text_terminal.h"
#include "scheduling/thread.h"
#include "memory/virtual_allocator.h"

namespace memory {

using containers::ObjectPool;
using containers::RecursiveInterruptSafeSpinlockGuard;
using hardware::BroadcastTlbShootdown;
using output::NumberFormat;
using output::print;
using scheduling::CpuCoreState;
using scheduling::GetCurrentCoreId;
using scheduling::GetCurrentCpuCore;

namespace {

// Bits pertaining to an entry in a page table.
namespace PageTableEntryBits {

// Indicates a page is present.
constexpr size_t kIsPresent = (1 << 0);

// Indicates a page is writable.
constexpr size_t kIsWritable = (1 << 1);

// Indicates a page is accessible in user space.
constexpr size_t kIsUserSpace = (1 << 2);

// Indicates a page is owned by this address space (a custom bit).
constexpr size_t kIsOwned = (1 << 9);

// Indicates a page is global and retained in TLB across CR3 reloads.
constexpr size_t kIsGlobal = (1 << 8);

// Indicates a page is not executable.
constexpr size_t kIsExecuteDisabled = (1L << 63L);

// Mask to extract the physical address from a page table entry.
constexpr size_t kPageAddressMask = 0x7FFFFFFFFFFFF000ULL;

}  // namespace PageTableEntryBits

// An initial statically allocated FreeMemoryRange representing
// the initial range of free memory before dynamic memory allocation is
// available.
VirtualAddressSpace::FreeMemoryRange g_initial_kernel_memory_range;

#ifdef TEST
// Currently active address space during unit tests.
VirtualAddressSpace* g_current_address_space = nullptr;
#endif

// A dud page table entry with all but the ownership and present bit set.
// A zeroed out entry indicates there's no page here, but this is
// actually reserved, such as for lazily allocated shared buffer.
constexpr size_t kDudPageEntry = (~(1 | (1 << 9)));

// The size of the page table, in bytes.
constexpr size_t kPageTableSize = 4096;  // 4 KB

// The size of a page table entry, in bytes.
constexpr size_t kPageTableEntrySize = 8;

// The number of entries in a page table. Each entry is 8 bytes long.
constexpr size_t kPageTableEntries = kPageTableSize / kPageTableEntrySize;

// The number of levels of page tables. (0 = PML4, 3 = PML1.)
constexpr int kNumPageTableLevels = 4;
// The deepest page table level.
constexpr int kDeepestPageTableLevel = kNumPageTableLevels - 1;

// Virtual addresses map into various page table (PML) levels
// 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
// 4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765
//                     #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^
//                     ^^^^ pml4       pml3       pml2       pml1   Single page

// The most significant bit used for indexing the top-most page table (PML4).
constexpr int kMostSignificantAddressBitInTopMostPageTable = 39;

// The number of address bits per page table level.
constexpr int kAddressBitsPerPageTableLevel = 9;

// The lowest address covered by PML4 entry 511. That entry is shared by
// reference with every address space and skipped during teardown, so no part of
// it may ever be handed out to a process.
constexpr size_t kLowestAddressInKernelPml4Entry = 0xFFFFFF8000000000ULL;

// How often, in iterations, to re-send a reschedule IPI while waiting for other
// cores to stop using an address space that is being destroyed.
constexpr size_t kCoresToVacateIpiInterval = 1 << 16;

// How many iterations to wait for other cores to stop using an address space
// that is being destroyed before giving up and leaking its page tables.
constexpr size_t kMaxIterationsWaitingForCoresToVacate = 1 << 24;

// The temporary mapping slots for every core share a single page table, which
// is allocated in InitializeKernelSpace.
static_assert(scheduling::kTotalTempSlots <= kPageTableEntries,
              "The per-core temporary mapping slots no longer fit in the "
              "single page table allocated for them.");

// Helper function for the destructor to recursively free pages and page tables.
// table_physical_address: The physical address of the current page table to
// scan. level: The current page table level (0 for PML4, 1 for PDPT, etc.).
inline void ScanAndFreePagesInLevel(size_t table_physical_address, int level) {
  // Temporarily map the current page table to access its entries.
  // The 'level' argument to TemporarilyMapPhysicalPages is an index for the
  // temporary mapping slot.
  size_t* current_table_virtual =
      (size_t*)TemporarilyMapPhysicalPages(table_physical_address, level);

  size_t max_entry_index = kPageTableEntries;
  if (level == 0) {
    // The PML4's last entry maps kernel space. Do not touch it.
    max_entry_index = kPageTableEntries - 1;
  }

  for (size_t i = 0; i < max_entry_index; i++) {
    size_t entry = current_table_virtual[i];
    if ((entry & PageTableEntryBits::kIsPresent) == 0) {
      // If the entry is not present, there's nothing here to free.
      continue;
    }

    // Extract the physical address from the entry.
    size_t pointed_physical_address = entry & ~(kPageSize - 1);

    if (level < kDeepestPageTableLevel) {
      // This entry points to a next-level page table.
      // Recursively call to free the contents of that next-level table.
      ScanAndFreePagesInLevel(pointed_physical_address, level + 1);
      // After the deeper table's contents are handled, free the physical page
      // of this next-level table itself.
      FreePhysicalPage(pointed_physical_address);
    } else {
      // This is the deepest level (e.g., Page Table), so entries point to data
      // pages. Only free the data page if it's marked as owned by this address
      // space.
      if (entry & PageTableEntryBits::kIsOwned) {
        FreePhysicalPage(pointed_physical_address);
      }
    }
  }
}

// Creates a page table entry creation with relevant flags.
uint64 CreatePageTableEntry(size_t physicaladdr, bool is_writable,
                            bool is_user_space, bool is_owned) {
  // Masked so that a caller-supplied address, which for MapPhysicalPages comes
  // straight from a driver, cannot smuggle in flag bits such as kIsOwned or
  // kIsGlobal.
  uint64 entry = (physicaladdr & PageTableEntryBits::kPageAddressMask) |
                 PageTableEntryBits::kIsPresent;  // Set the present bit.

  if (is_writable) entry |= PageTableEntryBits::kIsWritable;
  if (is_user_space) entry |= PageTableEntryBits::kIsUserSpace;
  if (is_owned) entry |= PageTableEntryBits::kIsOwned;

  return entry;
}

int CalculateIndexForAddressInPageTable(int page_table_level,
                                        size_t virtualaddr) {
  return (virtualaddr >> (kMostSignificantAddressBitInTopMostPageTable -
                          kAddressBitsPerPageTableLevel * page_table_level)) &
         ((1 << kAddressBitsPerPageTableLevel) - 1);
}

}  // namespace

VirtualAddressSpace::~VirtualAddressSpace() { ReleaseAllMemory(); }

void VirtualAddressSpace::ReleaseAllMemory() {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (IsKernelAddressSpace()) {
    print << "Cannot free kernel address space.\n";
    return;
  }
  // Switch to kernel space so the address space being freed isn't active.
#ifndef TEST
  if (GetCurrentCpuCore().current_address_space == this)
    KernelAddressSpace().SwitchToAddressSpace();

  // Every page table below is about to be handed back to the physical
  // allocator, so no other core may still be translating through them. Cores
  // switch to the kernel address space when they go idle and process teardown
  // has already terminated this address space's threads, so this normally
  // observes zero immediately. A reschedule IPI nudges any straggler into the
  // scheduler, which will move it off this address space.
  size_t iterations = 0;
  while (__atomic_load_n(&cores_using_this_address_space_, __ATOMIC_ACQUIRE) !=
         0) {
    if (iterations % kCoresToVacateIpiInterval == 0) {
      uint64 remaining =
          __atomic_load_n(&cores_using_this_address_space_, __ATOMIC_ACQUIRE);
      while (remaining != 0) {
        size_t core = static_cast<size_t>(__builtin_ctzll(remaining));
        remaining &= remaining - 1;
        hardware::SendRescheduleIpi(core);
      }
    }
    if (++iterations >= kMaxIterationsWaitingForCoresToVacate) {
      print << "Timed out waiting for other cores to stop using an address "
               "space being destroyed. Leaking its page tables.\n";
      return;
    }
    hardware::PollTlbShootdown();
    containers::CpuPause();
  }
#else
  if (g_current_address_space == this)
    KernelAddressSpace().SwitchToAddressSpace();
#endif

  // Free the memory pages owned by the address space and all of the tables.
  if (pml4_ == kOutOfMemory) return;
  ScanAndFreePagesInLevel(pml4_, 0);
  FreePhysicalPage(pml4_);
  pml4_ = kOutOfMemory;

  // Walk through the link of FreeMemoryRange objects and release them.
  while (auto fmr = free_memory_ranges_.PopFront())
    ObjectPool<FreeMemoryRange>::Release(fmr);
}

bool VirtualAddressSpace::InitializeUserSpace() {
  if (!CreateUserSpacePML4()) return false;
  unique_pages_ = 0;
  shared_pages_ = 0;

  // Set up what memory ranges are free. x86-64 processors use 48-bit canonical
  // addresses, split into lower-half and higher-half memory.

  // First, add the lower half memory.
  auto fmr = ObjectPool<FreeMemoryRange>::Allocate();
  if (fmr == nullptr) {
    FreePhysicalPage(pml4_);
    // The destructor runs after this returns false, and must not free the PML4
    // a second time.
    pml4_ = kOutOfMemory;
    return false;
  }

  size_t max_lower_half, min_higher_half;
  GetUserspaceVirtualMemoryHole(max_lower_half, min_higher_half,
                                /*inclusive=*/false);

  fmr->start_address = kPageSize;
  fmr->pages = (max_lower_half - kPageSize) / kPageSize;
  AddFreeMemoryRange(fmr);

  // Now add the higher half memory.
  fmr = ObjectPool<FreeMemoryRange>::Allocate();
  if (fmr != nullptr) {
    // NOTE: Gracefully continue if for some reason another FreeMemoryRange
    // could not be allocated.
    fmr->start_address = min_higher_half;
    // Stop below the PML4 entry that is shared with the kernel, which also
    // keeps this below kVirtualMemoryOffset where the kernel itself lives.
    fmr->pages =
        (kLowestAddressInKernelPml4Entry - min_higher_half) / kPageSize;
    AddFreeMemoryRange(fmr);
  }
  return true;
}

void VirtualAddressSpace::InitializeKernelSpace(
    size_t start_of_free_kernel_memory_at_boot, size_t& temp_memory_start,
    size_t*& temp_memory_page_table) {
  pml4_ = GetPhysicalPagePreVirtualMemory();

  // Clear the PML4.
  size_t* ptr = (size_t*)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml4_, 0);
  size_t i;
  for (i = 0; i < kPageTableEntries; i++) ptr[i] = 0;

  // Figure out what is the start of free memory, past the loaded code.
  size_t start_of_free_kernel_memory =
      ((size_t)g_start_of_free_memory_at_boot + kPageSize - 1) &
      ~(kPageSize - 1);  // Round up.

  // Map the booted code into memory.
  for (i = 0; i < start_of_free_kernel_memory; i += kPageSize)
    MapKernelMemoryPreVirtualMemory(i + kVirtualMemoryOffset, i, false);
  i += kVirtualMemoryOffset;

  // Allocate a virtual and physical page for our temporary page table.
  temp_memory_page_table = (size_t*)i;
  i += kPageSize;
  size_t physical_temp_memory_page_table = GetPhysicalPagePreVirtualMemory();
  MapKernelMemoryPreVirtualMemory((size_t)temp_memory_page_table,
                                  physical_temp_memory_page_table, false);

  // Maps the next 2MB range in memory for our temporary pages.
  size_t page_table_range = kPageSize * kPageTableEntries;
  temp_memory_start = (i + page_table_range) & ~(page_table_range - 1);

  size_t before_temp_memory = i;

  MapKernelMemoryPreVirtualMemory(temp_memory_start,
                                  physical_temp_memory_page_table, true);

  start_of_free_kernel_memory = temp_memory_start + (2 * 1024 * 1024);
  unique_pages_ =
      (start_of_free_kernel_memory - kVirtualMemoryOffset) / kPageSize;
  shared_pages_ = 0;

  // Hand create our first statically allocated FreeMemoryRange.
  g_initial_kernel_memory_range.is_static = true;
  g_initial_kernel_memory_range.start_address = start_of_free_kernel_memory;
  // Subtracting by 0 because the kernel lives at the top of the address space.
  g_initial_kernel_memory_range.pages =
      (0 - start_of_free_kernel_memory) / kPageSize;

  AddFreeMemoryRange(&g_initial_kernel_memory_range);

  if (before_temp_memory < temp_memory_start) {
    // The virtual address space had to be rounded up to align with 2MB for the
    // temporary page table. This is a free range.
    size_t num_pages = (temp_memory_start - before_temp_memory) / kPageSize;
    MarkAddressRangeAsFree(before_temp_memory, num_pages);
  }

#ifdef TEST
  // Set the current address space to a dud entry so SwitchToAddressSpace works.
  g_current_address_space = (VirtualAddressSpace*)nullptr;
#endif  // TEST
}

size_t VirtualAddressSpace::FindAndReserveFreePageRange(size_t pages) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (pages == 0) {
    // Too many or not enough entries.
    return kOutOfMemory;
  }

  // Find a free chunk of memory in the virtual address space that is either
  // equal to or greater than the requested size.
  FreeMemoryRange* fmr =
      free_chunks_by_size_.SearchForItemGreaterThanOrEqualToValue(pages);
  if (fmr == nullptr) {
    print << "Cannot find " << pages
          << " consecutive page(s) in the virtual address space.\n";
    PrintFreeAddressRanges();
    return kOutOfMemory;  // Virtual address space is full.
  }

  // print << "FindAndReserveFreePageRange at " << NumberFormat::Hexidecimal
  //      << fmr->start_address << " -> "
  //      << (fmr->start_address + fmr->pages * kPageSize) << "\n";

  RemoveFreeMemoryRange(fmr);
  if (fmr->pages == pages) {
    // This is exactly the size requested! The entire block can be used.
    size_t address = fmr->start_address;

    ObjectPool<FreeMemoryRange>::Release(fmr);

    return address;
  } else {
    // This memory address is larger than requested, so shrink it.
    size_t address = fmr->start_address;

    fmr->start_address += pages * kPageSize;
    fmr->pages -= pages;

    AddFreeMemoryRange(fmr);
    return address;
  }
}

bool VirtualAddressSpace::ReserveAddressRange(size_t address, size_t pages) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (pages == 0) {
    // Nothing was reserved, so this is not a successful reservation.
    return false;
  }
  FreeMemoryRange* fmr =
      free_chunks_by_address_.SearchForItemLessThanOrEqualToValue(address);
  if (fmr == nullptr) {
    return false;  // No free memory blocks in the area of interest.
  }

  size_t additional_pages_before = (address - fmr->start_address) / kPageSize;
  if (fmr->pages < additional_pages_before + pages) {
    return false;  // Free memory block can't fit in this address range.
  }

  RemoveFreeMemoryRange(fmr);

  if (fmr->start_address == address && fmr->pages == pages) {
    // This is exactly the size and location that is being requested.
    ObjectPool<FreeMemoryRange>::Release(fmr);
    return true;
  }

  size_t additional_pages_after =
      fmr->pages - (additional_pages_before + pages);

  // Allocate the FreeMemoryRanges to add back. Recycle `fmr` for one of them.
  FreeMemoryRange* fmr_before;
  FreeMemoryRange* fmr_after;
  if (additional_pages_before > 0 && additional_pages_after > 0) {
    fmr_before = fmr;
    fmr_after = ObjectPool<FreeMemoryRange>::Allocate();
    if (fmr_after == nullptr) {
      // Out of memory to allocate a new FreeMemoryRange object.
      AddFreeMemoryRange(fmr);
      return false;
    }
  } else if (additional_pages_before > 0) {
    fmr_before = fmr;
    fmr_after = nullptr;
  } else {
    fmr_before = nullptr;
    fmr_after = fmr;
  }

  // Add back the pages before.
  if (additional_pages_before > 0) {
    // As fmr_before = fmr, fmr_before->address is already set.
    fmr_before->pages = additional_pages_before;
    AddFreeMemoryRange(fmr_before);
  }

  // Add back the pages after.
  if (additional_pages_after > 0) {
    fmr_after->start_address = address + pages * kPageSize;
    fmr_after->pages = additional_pages_after;
    AddFreeMemoryRange(fmr_after);
  }
  return true;
}

size_t VirtualAddressSpace::AllocatePages(size_t pages) {
  return AllocatePagesBelowMaxBaseAddress(pages, 0xFFFFFFFFFFFFFFFF);
}

size_t VirtualAddressSpace::AllocatePagesBelowMaxBaseAddress(
    size_t pages, size_t max_base_address) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  size_t start = FindAndReserveFreePageRange(pages);
  if (start == kOutOfMemory) return kOutOfMemory;

  // Allocate each page in the range.
  size_t addr = start;
  for (size_t i = 0; i < pages; i++, addr += kPageSize) {
    // Get a physical page.
    size_t phys = GetPhysicalPageAtOrBelowAddress(max_base_address);

    bool success = true;
    if (phys == kOutOfPhysicalPages) {
      // No physical pages. Unmap all memory until this point.
      print << "Out of physical pages.\n";
      success = false;
    }

    // Map the physical page.
    if (success && !MapPhysicalPageAt(addr, phys, true, true, false)) {
      print << "Call to MapPhysicalPage failed.\n";
      FreePhysicalPage(phys);
      success = false;
    }

    if (!success) {
      // Pages before `addr` are mapped, so they need unmapping as well as
      // returning to the free ranges. `addr` itself and everything after it was
      // never mapped, so it only needs returning to the free ranges. Freeing
      // them all with FreePages would silently drop the unmapped ones.
      size_t mapped_pages = (addr - start) / kPageSize;
      if (mapped_pages > 0) FreePages(start, mapped_pages);
      MarkAddressRangeAsFree(addr, pages - mapped_pages);
      return kOutOfMemory;
    }
  }

  return start;
}

void VirtualAddressSpace::ReleasePages(size_t addr, size_t pages) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (!IsPageAlignedAddress(addr) || pages == 0) return;
  size_t bytes = pages * kPageSize;
  if (bytes / kPageSize != pages || addr + bytes < addr) return;
  if (!IsAddressInCorrectSpace(addr) || !IsAddressInCorrectSpace(addr + bytes - 1))
    return;

  for (size_t i = 0; i < pages; i++, addr += kPageSize)
    UnmapVirtualPage(addr, /*free=*/false);
}

void VirtualAddressSpace::FreePages(size_t addr, size_t pages) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (!IsPageAlignedAddress(addr) || pages == 0) return;
  size_t bytes = pages * kPageSize;
  if (bytes / kPageSize != pages || addr + bytes < addr) return;
  if (!IsAddressInCorrectSpace(addr) || !IsAddressInCorrectSpace(addr + bytes - 1))
    return;

  for (size_t i = 0; i < pages; i++, addr += kPageSize)
    UnmapVirtualPage(addr, /*free=*/true);
}

size_t VirtualAddressSpace::MapPhysicalPages(size_t addr, size_t pages) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (pages == 0 || pages > kMaxPagesPerSyscall) return kOutOfMemory;
  // The mapping covers whole pages, so map the page containing `addr`.
  addr = RoundDownToPageAlignedAddress(addr);

  size_t start_virtual_address = FindAndReserveFreePageRange(pages);
  if (start_virtual_address == kOutOfMemory) return kOutOfMemory;

  for (size_t virtual_address = start_virtual_address; pages > 0;
       pages--, virtual_address += kPageSize, addr += kPageSize) {
    MapPhysicalPageAt(virtual_address, addr, false, true, false);
  }
  return start_virtual_address;
}

bool VirtualAddressSpace::MapPhysicalPageAt(size_t virtualaddr,
                                            size_t physicaladdr, bool own,
                                            bool can_write,
                                            bool throw_exception_on_access) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  return MapPhysicalPageImpl(virtualaddr, physicaladdr,
                             TemporarilyMapPhysicalPages, GetPhysicalPage, own,
                             can_write, throw_exception_on_access,
                             /*assign_page_table=*/false);
}

void VirtualAddressSpace::MarkAddressRangeAsFree(size_t address, size_t pages) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  // Search for a block right before.
  FreeMemoryRange* block_before =
      free_chunks_by_address_.SearchForItemLessThanOrEqualToValue(address);

  if (block_before != nullptr) {
    if (block_before->start_address == address) {
      print << "Error: block_before->start_address == address\n";
      return;
    }
    if (block_before->start_address + (block_before->pages * kPageSize) >
        address) {
      print << "Error: block_before->start_address + (block_before->pages * "
               "kPageSize) > address\n Trying to free address "
            << NumberFormat::Hexidecimal << address << ' ';
      PrintFreeAddressRanges();
      print << "Before block: " << NumberFormat::Hexidecimal
            << block_before->start_address << " -> "
            << (block_before->start_address + (block_before->pages * kPageSize))
            << '\n';

      free_chunks_by_address_.PrintAATree();
      return;
    }

    if (block_before->start_address + (block_before->pages * kPageSize) !=
        address) {
      // The previous block doesn't touch the start of this address range.
      block_before = nullptr;
    }
  }

  // Search for a block right after.
  FreeMemoryRange* block_after =
      free_chunks_by_address_.SearchForItemEqualToValue(address +
                                                        (pages * kPageSize));

  if (block_before != nullptr) {
    RemoveFreeMemoryRange(block_before);

    if (block_after != nullptr) {
      // Merge into the block before and after
      RemoveFreeMemoryRange(block_after);
      // Expand the size of the block before.
      block_before->pages += pages + block_after->pages;
      AddFreeMemoryRange(block_before);
      // Release the block after since it was merged in.
      ObjectPool<FreeMemoryRange>::Release(block_after);
    } else {
      // Merge into the block before.
      // Expand the size of the block before.
      block_before->pages += pages;
      AddFreeMemoryRange(block_before);
    }
  } else if (block_after != nullptr) {
    // Merge into the block after.
    RemoveFreeMemoryRange(block_after);
    // Expand and pull back the size of the block after.
    block_after->start_address = address;
    block_after->pages += pages;
    AddFreeMemoryRange(block_after);

  } else {
    // Stand alone free memory range that can't merge into anything.
    auto fmr = ObjectPool<FreeMemoryRange>::Allocate();
    if (fmr == nullptr) {
      return;
    }
    fmr->start_address = address;
    fmr->pages = pages;
    AddFreeMemoryRange(fmr);
  }
}

size_t VirtualAddressSpace::GetPhysicalAddress(size_t virtualaddr,
                                               bool ignore_unowned_pages) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (!IsAddressInCorrectSpace(virtualaddr)) return kOutOfMemory;
  size_t last_entry = pml4_;
  // Walk the page table hierarchy.
  for (int level = 0; level < kNumPageTableLevels; level++) {
    size_t* table = static_cast<size_t*>(
        TemporarilyMapPhysicalPages(last_entry & ~(kPageSize - 1), level));
    last_entry = table[CalculateIndexForAddressInPageTable(level, virtualaddr)];
    // Check that the entry is valid.
    if ((last_entry & PageTableEntryBits::kIsPresent) == 0) return kOutOfMemory;
  }

  // Check if the caller wants to ignore unowned pages and this entry is for an
  // unowned page.
  if (ignore_unowned_pages && (last_entry & PageTableEntryBits::kIsOwned) == 0)
    return kOutOfMemory;

  // Return the address of this entry, masking out flags and status bits (such
  // as the No-Execute bit in bit 63).
  return last_entry & PageTableEntryBits::kPageAddressMask;
}

size_t VirtualAddressSpace::GetOrCreateVirtualPage(size_t virtualaddr) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  size_t physical_address = GetPhysicalAddress(virtualaddr,
                                               /*ignore_unowned_pages=*/false);
  if (physical_address != kOutOfMemory) return physical_address;

  physical_address = GetPhysicalPage();
  if (physical_address == kOutOfPhysicalPages) return kOutOfMemory;

  // TODO: Investigate what happens if this were lazily allocated page not yet
  // allocated.
  if (!MarkVirtualAddressAsUsed(virtualaddr)) {
    FreePhysicalPage(physical_address);
    return kOutOfMemory;
  }

  if (MapPhysicalPageAt(virtualaddr, physical_address, true, true, false)) {
    return physical_address;
  } else {
    if (virtualaddr != 0) MarkAddressRangeAsFree(virtualaddr, 1);
    FreePhysicalPage(physical_address);
    return kOutOfMemory;
  }
}

void VirtualAddressSpace::PrintFreeAddressRanges() {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  print << "Free address ranges:\n" << NumberFormat::Hexidecimal;
  for (auto* fmr : free_memory_ranges_) {
    print << ' ' << fmr->start_address << "->"
          << (fmr->start_address + kPageSize * fmr->pages) << '\n';
  }
}

void VirtualAddressSpace::SetMemoryAccessRights(size_t address, size_t rights) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (!IsAddressInCorrectSpace(address)) return;

  size_t last_entry = pml4_;
  size_t* table = nullptr;
  size_t last_index = 0;
  // Walk the page table hierarchy.
  for (int level = 0; level < kNumPageTableLevels; level++) {
    table = static_cast<size_t*>(
        TemporarilyMapPhysicalPages(last_entry & ~(kPageSize - 1), level));
    last_index = CalculateIndexForAddressInPageTable(level, address);
    last_entry = table[last_index];
    // Check that the entry is valid.
    if ((last_entry & PageTableEntryBits::kIsPresent) == 0) return;
  }

  // Check that the address space owns the page.
  if ((last_entry & PageTableEntryBits::kIsOwned) == 0) return;

  // Remove the bits that might be set.
  last_entry &= ~(PageTableEntryBits::kIsExecuteDisabled |
                  PageTableEntryBits::kIsWritable);

  // Set the relevant bits.
  if (rights & MemoryAccessRights::kWriteAccess)
    last_entry |= PageTableEntryBits::kIsWritable;
  if ((rights & MemoryAccessRights::kExecuteAccess) == 0)
    last_entry |= PageTableEntryBits::kIsExecuteDisabled;

  table[last_index] = last_entry;
  FlushAddressIfActive(address, IsKernelAddress(address));
}

void VirtualAddressSpace::FlushAddressIfActive(size_t virtualaddr,
                                               bool is_kernel_address) {
#ifndef TEST
  bool is_active_on_current_core =
      (GetCurrentCpuCore().current_address_space == this);
  if (is_active_on_current_core || is_kernel_address)
    FlushVirtualPage(virtualaddr);

  // Kernel pages are mapped into every address space, so every core has to be
  // told regardless of what it currently has loaded.
  uint64 other_cores =
      __atomic_load_n(&cores_using_this_address_space_, __ATOMIC_ACQUIRE) &
      ~(1ULL << GetCurrentCoreId());
  if (other_cores != 0 || is_kernel_address)
    BroadcastTlbShootdown(virtualaddr);
#else
  if (this == g_current_address_space || is_kernel_address)
    FlushVirtualPage(virtualaddr);
#endif
}

#ifdef TEST
extern "C" size_t mock_cr3;
#endif

void VirtualAddressSpace::SwitchToAddressSpace() {
#ifndef TEST
  CpuCoreState& cpu = GetCurrentCpuCore();
  VirtualAddressSpace* previous = cpu.current_address_space;
  if (previous == this) return;

  uint64 core_bit = 1ULL << GetCurrentCoreId();
  // Claimed before CR3 is loaded, so a concurrent shootdown can never decide
  // this core is not using the address space while it already is.
  __atomic_fetch_or(&cores_using_this_address_space_, core_bit,
                    __ATOMIC_SEQ_CST);
  __atomic_store_n(&cpu.current_address_space, this, __ATOMIC_RELEASE);
  __asm__ __volatile__("mov %0, %%cr3" ::"b"(pml4_) : "memory");

  // Released only after CR3 no longer points at the previous address space.
  if (previous != nullptr)
    __atomic_fetch_and(&previous->cores_using_this_address_space_, ~core_bit,
                       __ATOMIC_SEQ_CST);
#else
  if (this != g_current_address_space) {
    g_current_address_space = this;
    mock_cr3 = pml4_;
  }
#endif
}

VirtualAddressSpace& VirtualAddressSpace::CurrentAddressSpace() {
#ifndef TEST
  VirtualAddressSpace* space = GetCurrentCpuCore().current_address_space;
#else
  VirtualAddressSpace* space = g_current_address_space;
#endif
  // A core that has not switched address space yet is running on the page
  // tables the kernel booted with.
  if (space == nullptr) return KernelAddressSpace();
  return *space;
}

bool VirtualAddressSpace::IsKernelAddressSpace() {
  return this == &KernelAddressSpace();
}

bool VirtualAddressSpace::CreateUserSpacePML4() {
  pml4_ = GetPhysicalPage();
  if (pml4_ == kOutOfPhysicalPages) {
    pml4_ = kOutOfMemory;
    return false;
  }

  // Clear out this virtual address space.
  size_t* ptr = (size_t*)TemporarilyMapPhysicalPages(pml4_, 0);
  for (size_t i = 0; i < kPageTableEntries - 1; i++) ptr[i] = 0;

  // Copy the kernel's address space into this.
  size_t* kernel_ptr =
      (size_t*)TemporarilyMapPhysicalPages(KernelAddressSpace().pml4_, 1);
  ptr[kPageTableEntries - 1] = kernel_ptr[kPageTableEntries - 1];

  return true;
}

void VirtualAddressSpace::MapKernelMemoryPreVirtualMemory(
    size_t virtualaddr, size_t physicaladdr, bool assign_page_table) {
  if (!MapPhysicalPageImpl(virtualaddr, physicaladdr,
                           TemporarilyMapPhysicalMemoryPreVirtualMemory,
                           GetPhysicalPagePreVirtualMemory,
                           /*own=*/true, /*can_write=*/true,
                           /*throw_exception_on_access=*/false,
                           assign_page_table)) {
    print << "Out of memory during kernel initialization.\n";
#ifndef TEST
    __asm__ __volatile__("hlt");
#endif
  }
}

bool VirtualAddressSpace::MarkVirtualAddressAsUsed(size_t address) {
  FreeMemoryRange* block_before =
      free_chunks_by_address_.SearchForItemLessThanOrEqualToValue(address);

  // Check if memory is already occupied.
  if (block_before == nullptr) return false;

  // Check if memory is already occupied.
  if (block_before->start_address + (block_before->pages * kPageSize) <=
      address)
    return false;

  // print << "Marking " << NumberFormat::Hexidecimal << address << " as
  // used.\n";

  RemoveFreeMemoryRange(block_before);

  if (block_before->start_address == address) {
    if (block_before->pages == 1) {
      // Exactly the size needed.
      ObjectPool<FreeMemoryRange>::Release(block_before);
    } else {
      // Bump up this free memory range and re-add it.
      block_before->start_address += kPageSize;
      block_before->pages--;
      AddFreeMemoryRange(block_before);
    }
  } else if (block_before->start_address +
                 ((block_before->pages - 1) * kPageSize) ==
             address) {
    // Bump this free memory range down and re-add it.
    block_before->pages--;
    AddFreeMemoryRange(block_before);
  } else {
    // Split this free memory block into two.
    auto block_after = ObjectPool<FreeMemoryRange>::Allocate();
    if (block_after == nullptr) {
      // Out of memory, undo the block removal.
      AddFreeMemoryRange(block_before);
      return false;
    }

    // Calculate how many free pages there will be before and after this page
    // is removed.
    size_t pages_before = (address - block_before->start_address) / kPageSize;
    size_t pages_after = block_before->pages - pages_before - 1;

    block_before->pages = pages_before;
    AddFreeMemoryRange(block_before);

    block_after->start_address = address + kPageSize;
    block_after->pages = pages_after;
    AddFreeMemoryRange(block_after);
  }

  return true;
}

bool VirtualAddressSpace::MapPhysicalPageImpl(
    size_t virtualaddr, size_t physicaladdr,
    void* (*temporarily_map_physical_memory)(size_t addr, size_t index),
    size_t (*get_physical_page)(), bool own, bool can_write,
    bool throw_exception_on_access, bool assign_page_table) {
  if (!IsAddressInCorrectSpace(virtualaddr)) return false;
  bool is_kernel_address = IsKernelAddress(virtualaddr);

  // The physical addresses of the tables at each level.
  size_t table_addr[kNumPageTableLevels];
  // Whether the table at this level was allocated during this call.
  bool allocated_table[kNumPageTableLevels];
  // The mapped tables at each level.
  size_t* tables[kNumPageTableLevels];

  // Populate the highest level.
  table_addr[0] = pml4_;
  allocated_table[0] = false;
  tables[0] = (size_t*)temporarily_map_physical_memory(table_addr[0], 0);

  // Walk the page table hierarchy, creating tables as needed.
  for (int level = 0; level < kDeepestPageTableLevel; level++) {
    int index = CalculateIndexForAddressInPageTable(level, virtualaddr);
    if (assign_page_table && level == kNumPageTableLevels - 2) {
      // Mapping a page table into memory (this is used for mapping the
      // page table used for temporarily accessing memory). This gets applied
      // at the second to last level (PML2).
      size_t& entry = tables[level][index];
      if (entry != 0) {
        // print << "Attempting to map page table to a location where there's "
        //         "already a page table..\n";
        return false;
      }
      entry = CreatePageTableEntry(physicaladdr, /*is_writable=*/true,
                                   !is_kernel_address, /*is_owned=*/false);
      return true;
    }
    if (tables[level][index] == 0) {
      // Entry is blank, create a new table.
      size_t new_table_physicaladdr = get_physical_page();
      if (new_table_physicaladdr == kOutOfPhysicalPages) {
        // Deallocate any pages that were allocated this call.
        for (int level_to_deallocate = level; level_to_deallocate >= 1;
             level_to_deallocate--) {
          if (allocated_table[level_to_deallocate]) {
            // This table was allocated this call, so free it.
            FreePhysicalPage(table_addr[level_to_deallocate]);
            // Erase it in the parent table.
            int index_in_parent = CalculateIndexForAddressInPageTable(
                level_to_deallocate - 1, virtualaddr);
            tables[level_to_deallocate - 1][index_in_parent] = 0;
          }
        }
        return false;
      }
      // Write in the page table entry.
      tables[level][index] =
          CreatePageTableEntry(new_table_physicaladdr, /*is_writable=*/true,
                               !is_kernel_address, /*is_owned=*/false);
      table_addr[level + 1] = new_table_physicaladdr;

      // Map it into memory.
      tables[level + 1] = (size_t*)temporarily_map_physical_memory(
          new_table_physicaladdr, level + 1);
      // Clear the new table.
      for (int i = 0; i < kPageTableEntries; i++) tables[level + 1][i] = 0;

      allocated_table[level + 1] = true;
    } else {
      // Entry is not blank, map it into memory.
      table_addr[level + 1] = tables[level][index] & ~(kPageSize - 1);
      // Map in the new table.
      tables[level + 1] = (size_t*)temporarily_map_physical_memory(
          table_addr[level + 1], level + 1);
      allocated_table[level + 1] = false;
    }
  }
  // Get the entry in the deepest page table level (PML1).
  size_t& entry =
      tables[kDeepestPageTableLevel][CalculateIndexForAddressInPageTable(
          kDeepestPageTableLevel, virtualaddr)];
  if (entry != 0 && entry != kDudPageEntry) {
    if ((entry & PageTableEntryBits::kPageAddressMask) == physicaladdr) {
      FlushAddressIfActive(virtualaddr, is_kernel_address);
      return true;
    }
    // Don't worry about cleaning up PML2/3 because for it to be mapped PML2/3
    // must already exist.
    print << "Mapping page to " << NumberFormat::Hexidecimal << virtualaddr
          << " but something is already there.\n";
    return false;
  }

  size_t old_entry = entry;

  // Write the new entry in the PML1.
  entry = throw_exception_on_access
              ? kDudPageEntry
              : CreatePageTableEntry(physicaladdr, can_write,
                                     !is_kernel_address, own);
  if (is_kernel_address && !throw_exception_on_access)
    entry |= PageTableEntryBits::kIsGlobal;

  if (old_entry == 0) {
    if (throw_exception_on_access) {
      shared_pages_++;
    } else {
      if (own) {
        unique_pages_++;
      } else {
        shared_pages_++;
      }
    }
  } else if (old_entry == kDudPageEntry) {
    if (!throw_exception_on_access) {
      if (own) {
        shared_pages_--;
        unique_pages_++;
      }
    }
  }

  FlushAddressIfActive(virtualaddr, is_kernel_address);
  return true;
}

// Unmaps a virtual page - free specifies if that page should be returned to
// the physical memory manager.
void VirtualAddressSpace::UnmapVirtualPage(size_t virtualaddr, bool free) {
  RecursiveInterruptSafeSpinlockGuard guard(lock_);
  if (!IsAddressInCorrectSpace(virtualaddr)) return;

  if (!IsPageAlignedAddress(virtualaddr)) {
    print << "UnmapVirtualPage called with non page aligned "
             " address: "
          << NumberFormat::Hexidecimal << virtualaddr << '\n';
    virtualaddr = RoundDownToPageAlignedAddress(virtualaddr);
  }

  // The physical addresses of the tables at each level.
  size_t table_addr[kNumPageTableLevels];
  // Whether the table at this level was allocated during this call.
  bool allocated_table[kNumPageTableLevels];
  // The mapped tables at each level.
  size_t* tables[kNumPageTableLevels];

  // Populate the highest level.
  table_addr[0] = pml4_;
  tables[0] = (size_t*)TemporarilyMapPhysicalPages(table_addr[0], 0);

  // Walk the page table hierarchy.
  for (int level = 0; level < kDeepestPageTableLevel; level++) {
    int index = CalculateIndexForAddressInPageTable(level, virtualaddr);
    // Nothing to do if the entry is blank.
    if (tables[level][index] == 0) return;
    // Entry is not blank, map it into memory.
    table_addr[level + 1] = tables[level][index] & ~(kPageSize - 1);
    // Map in the new table.
    tables[level + 1] =
        (size_t*)TemporarilyMapPhysicalPages(table_addr[level + 1], level + 1);
  }

  // Get the entry in the deepest page table level (PML1).
  size_t& entry =
      tables[kDeepestPageTableLevel][CalculateIndexForAddressInPageTable(
          kDeepestPageTableLevel, virtualaddr)];

  if (entry == 0) return;
  if (free && (entry & PageTableEntryBits::kIsOwned) == 0)
    return;  // Cannot free unowned memory pages (e.g. shared memory or MMIO).

  // Note the page to free rather than freeing it here. It is still present and
  // writable in this address space and in every remote core's TLB, so handing
  // it back to the physical allocator now would let another process be given a
  // page that a thread of this one can still write to. Freeing is optional
  // because shared memory and memory mapped IO can be unmapped without freeing
  // the physical pages.
  size_t physical_page_to_free = kOutOfPhysicalPages;
  if (free && (entry & PageTableEntryBits::kIsOwned) != 0)
    physical_page_to_free = entry & ~(kPageSize - 1);

  if (entry == kDudPageEntry) {
    shared_pages_--;
  } else if (entry != 0) {
    if ((entry & PageTableEntryBits::kIsOwned) != 0) {
      unique_pages_--;
    } else {
      shared_pages_--;
    }
  }

  // Remove this entry for the deepest page table.
  entry = 0;

  FlushAddressIfActive(virtualaddr, IsKernelAddress(virtualaddr));

  // Scan the page tables to see if they are completely empty so that the
  // physical pages can be released. Don't release the shallowest level (the
  // PML4). Like the data page, these are freed only after the invalidation
  // below.
  size_t page_tables_to_free[kNumPageTableLevels];
  int page_tables_to_free_count = 0;
  for (int level = kDeepestPageTableLevel; level > 0; level--) {
    // Stop if there's anything still in the table.
    bool is_empty = true;
    for (int i = 0; i < kPageTableEntries; i++) {
      if (tables[level][i] != 0) {
        is_empty = false;
        break;
      }
    }
    if (!is_empty) break;

    page_tables_to_free[page_tables_to_free_count++] = table_addr[level];
    // Mark the entry as free in the parent table.
    tables[level - 1]
          [CalculateIndexForAddressInPageTable(level - 1, virtualaddr)] = 0;
  }

  if (page_tables_to_free_count > 0) {
    // Processors cache intermediate paging structures separately from final
    // translations, and invalidating a single address does not evict them. A
    // core could otherwise keep translating through a page table that has been
    // returned to the allocator and reissued as data.
    hardware::FlushEntireTlbIncludingGlobalPages();
    BroadcastTlbShootdown(hardware::kFlushEntireTlb);

    for (int i = 0; i < page_tables_to_free_count; i++)
      FreePhysicalPage(page_tables_to_free[i]);
  }

  if (physical_page_to_free != kOutOfPhysicalPages)
    FreePhysicalPage(physical_page_to_free);

  // `tables` and `table_addr` must not be used past this point.
  // MarkAddressRangeAsFree can allocate a FreeMemoryRange, which can grow the
  // kernel heap, which re-maps the very temporary slots `tables` points into.
  if (virtualaddr != 0) MarkAddressRangeAsFree(virtualaddr, 1);
}

void VirtualAddressSpace::AddFreeMemoryRange(FreeMemoryRange* fmr) {
  if (!IsPageAlignedAddress(fmr->start_address)) {
    print << "AddFreeMemoryRange called with non page "
             "aligned address: "
          << NumberFormat::Hexidecimal << fmr->start_address << '\n';
    return;
  }

  free_chunks_by_address_.Insert(fmr);
  free_chunks_by_size_.Insert(fmr);
  free_memory_ranges_.AddFront(fmr);
}

void VirtualAddressSpace::RemoveFreeMemoryRange(FreeMemoryRange* fmr) {
  free_chunks_by_address_.Remove(fmr);
  free_chunks_by_size_.Remove(fmr);
  free_memory_ranges_.Remove(fmr);
}

bool VirtualAddressSpace::IsAddressInCorrectSpace(size_t virtualaddr) {
  bool is_kernel_address = IsKernelAddress(virtualaddr);
  bool is_kernel_address_space = this == &KernelAddressSpace();
  return is_kernel_address == is_kernel_address_space;
}

}  // namespace memory

