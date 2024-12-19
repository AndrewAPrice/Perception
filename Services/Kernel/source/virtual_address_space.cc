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
#include "virtual_address_space.h"

#include "object_pool.h"
#include "physical_allocator.h"
#include "virtual_allocator.h"

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

// Indicates a page is not executable.
constexpr size_t kIsExecuteDisabled = (1L << 63L);

}  // namespace PageTableEntryBits

// An initial statically allocated FreeMemoryRange that we can use to represent
// the initial range of free memory before we can dynamically allocate memory.
VirtualAddressSpace::FreeMemoryRange initial_kernel_memory_range;

// The currently loaded virtual address space.
VirtualAddressSpace *current_address_space = nullptr;

// A dud page table entry with all but the ownership and present bit set.
// We use a zeroed out entry to mean there's no page here, but this is
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

// The most significant bit in the top most page table.
constexpr int kMostSignificantAddressBitInTopMostPageTable = 39;

// The number of address bits per page table level.
constexpr int kAddressBitsPerPageTableLevel = 9;

inline void ScanAndFreePagesInLevel(size_t table_address, int level) {
  bool is_shallowest_level = level == 0;
  bool is_deepest_level = level == kDeepestPageTableLevel;
  size_t *table = (size_t *)TemporarilyMapPhysicalPages(table_address, level);

  // On the shallowest level, skip the last entry as it maps into kernel memory.
  size_t max_entry =
      is_shallowest_level ? kPageTableEntries - 1 : kPageTableEntries;
  for (int i = 0; i < max_entry; i++) {
    size_t entry = table[i];
    if (is_shallowest_level) {
      if (entry &
          (PageTableEntryBits::kIsPresent | PageTableEntryBits::kIsOwned)) {
        // Free the found page.
        FreePhysicalPage(entry & ~(PAGE_SIZE - 1));
      }
    } else if (entry) {
      // Scan one level deeper then free this page.
      size_t physical_address = entry & ~(PAGE_SIZE - 1);
      ScanAndFreePagesInLevel(physical_address, level + 1);
      FreePhysicalPage(physical_address);
    }
  }
}

// Creates a page table entry creation with relevant flags.
uint64 CreatePageTableEntry(size_t physicaladdr, bool is_writable,
                            bool is_user_space, bool is_owned) {
  uint64 entry =
      physicaladdr | PageTableEntryBits::kIsPresent;  // Set the present bit.

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

VirtualAddressSpace::~VirtualAddressSpace() {
  // Switch to kernel space so the address space being freed isn't active.
  if (current_address_space == this)
    KernelAddressSpace().SwitchToAddressSpace();

  // Free the memory pages owned by the address space and all of the tables.
  if (pml4_ == OUT_OF_MEMORY) return;
  ScanAndFreePagesInLevel(pml4_, 0);
  FreePhysicalPage(pml4_);

  // Walk through the link of FreeMemoryRange objects and release them.
  while (auto fmr = free_memory_ranges_.PopFront())
    ObjectPool<FreeMemoryRange>::Release(fmr);
}

bool VirtualAddressSpace::InitializeUserSpace() {
  if (!CreateUserSpacePML4()) return false;

  // Set up what memory ranges are free. x86-64 processors use 48-bit canonical
  // addresses, split into lower-half and higher-half memory.

  // First, add the lower half memory.
  auto fmr = ObjectPool<FreeMemoryRange>::Allocate();
  if (fmr == nullptr) {
    FreePhysicalPage(pml4_);
    return false;
  }

  size_t max_lower_half, min_higher_half;
  GetUserspaceVirtualMemoryHole(max_lower_half, min_higher_half,
                                /*inclusive=*/false);

  fmr->start_address = 0;
  fmr->pages = max_lower_half / PAGE_SIZE;
  AddFreeMemoryRange(fmr);

  // Now add the higher half memory.
  fmr = ObjectPool<FreeMemoryRange>::Allocate();
  if (fmr != nullptr) {
    // We can gracefully continue if for some reason we couldn't allocate
    // another FreeMemoryRange.

    fmr->start_address = min_higher_half;
    // Don't go too high - the kernel lives up there.
    fmr->pages = (VIRTUAL_MEMORY_OFFSET - min_higher_half) / PAGE_SIZE;
    AddFreeMemoryRange(fmr);
  }
  return true;
}

void VirtualAddressSpace::InitializeKernelSpace(
    size_t start_of_free_kernel_memory_at_boot, size_t &temp_memory_start,
    size_t *&temp_memory_page_table) {
  pml4_ = GetPhysicalPagePreVirtualMemory();

  // Clear the PML4.
  size_t *ptr =
      (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml4_, 0);
  size_t i;
  for (i = 0; i < kPageTableEntries; i++) ptr[i] = 0;

  // Figure out what is the start of free memory, past the loaded code.
  size_t start_of_free_kernel_memory =
      ((size_t)start_of_free_memory_at_boot + PAGE_SIZE - 1) &
      ~(PAGE_SIZE - 1);  // Round up.

  // Map the booted code into memory.
  for (i = 0; i < start_of_free_kernel_memory; i += PAGE_SIZE)
    MapKernelMemoryPreVirtualMemory(i + VIRTUAL_MEMORY_OFFSET, i, false);
  i += VIRTUAL_MEMORY_OFFSET;

  // Allocate a virtual and physical page for our temporary page table.
  temp_memory_page_table = (size_t *)i;
  i += PAGE_SIZE;
  size_t physical_temp_memory_page_table = GetPhysicalPagePreVirtualMemory();
  MapKernelMemoryPreVirtualMemory((size_t)temp_memory_page_table,
                                  physical_temp_memory_page_table, false);

  // Maps the next 2MB range in memory for our temporary pages.
  size_t page_table_range = PAGE_SIZE * kPageTableEntries;
  temp_memory_start = (i + page_table_range) & ~(page_table_range - 1);

  size_t before_temp_memory = i;

  MapKernelMemoryPreVirtualMemory(temp_memory_start,
                                  physical_temp_memory_page_table, true);

  start_of_free_kernel_memory = temp_memory_start + (2 * 1024 * 1024);

  // Hand create our first statically allocated FreeMemoryRange.
  initial_kernel_memory_range.start_address = start_of_free_kernel_memory;
  // Subtracting by 0 because the kernel lives at the top of the address space.
  initial_kernel_memory_range.pages =
      (0 - start_of_free_kernel_memory) / PAGE_SIZE;

  AddFreeMemoryRange(&initial_kernel_memory_range);

  if (before_temp_memory < temp_memory_start) {
    // The virtual address space had to be rounded up to align with 2MB for the
    // temporary page table. This is a free range.
    size_t num_pages = (temp_memory_start - before_temp_memory) / PAGE_SIZE;
    MarkAddressRangeAsFree(before_temp_memory, num_pages);
  }

  // Set the current address space to a dud entry so SwitchToAddressSpace works.
  current_address_space = (VirtualAddressSpace *)nullptr;
}

size_t VirtualAddressSpace::FindAndReserveFreePageRange(size_t pages) {
  if (pages == 0) {
    // Too many or not enough entries.
    return OUT_OF_MEMORY;
  }

  // Find a free chunk of memory in the virutal address space that is either
  // equal to or greater than what we need.
  FreeMemoryRange *fmr =
      free_chunks_by_size_.SearchForItemGreaterThanOrEqualToValue(pages);
  if (fmr == nullptr) return OUT_OF_MEMORY;  // Virtual address space is full.

  // print << "FindAndReserveFreePageRange at " << NumberFormat::Hexidecimal
  //      << fmr->start_address << " -> "
  //      << (fmr->start_address + fmr->pages * PAGE_SIZE) << "\n";

  RemoveFreeMemoryRange(fmr);
  if (fmr->pages == pages) {
    // This is exactly the size we need! We can use this whole block.
    size_t address = fmr->start_address;

    ObjectPool<FreeMemoryRange>::Release(fmr);

    return address;
  } else {
    // This memory address is bigger than what we need, so shrink it.
    size_t address = fmr->start_address;

    fmr->start_address += pages * PAGE_SIZE;
    fmr->pages -= pages;

    AddFreeMemoryRange(fmr);
    return address;
  }
}

bool VirtualAddressSpace::ReserveAddressRange(size_t address, size_t pages) {
  if (pages == 0) {
    // Too many or not enough entries.
    return OUT_OF_MEMORY;
  }
  FreeMemoryRange *fmr =
      free_chunks_by_address_.SearchForItemLessThanOrEqualToValue(address);
  if (fmr == nullptr) {
    return false;  // No free memory blocks in the area of interest.
  }

  size_t additional_pages_before = (address - fmr->start_address) / PAGE_SIZE;
  if (fmr->pages < additional_pages_before + pages) {
    return false;  // Free memory block can't fit in this address range.
  }

  RemoveFreeMemoryRange(fmr);

  if (fmr->start_address && fmr->pages == pages) {
    // This is exactly the size and location that is being requested.
    ObjectPool<FreeMemoryRange>::Release(fmr);
    return true;
  }

  size_t additional_pages_after =
      fmr->pages - (additional_pages_before + pages);

  // Allocate the FreeMemoryRanges to add back. Recycle `fmr` for one of them.
  FreeMemoryRange *fmr_before;
  FreeMemoryRange *fmr_after;
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
    fmr_after->start_address = address + pages * PAGE_SIZE;
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
  size_t start = FindAndReserveFreePageRange(pages);
  if (start == OUT_OF_MEMORY) return 0;

  // Allocate each page we've found.
  size_t addr = start;
  for (size_t i = 0; i < pages; i++, addr += PAGE_SIZE) {
    // Get a physical page.
    size_t phys = GetPhysicalPageAtOrBelowAddress(max_base_address);

    bool success = true;
    if (phys == OUT_OF_PHYSICAL_PAGES) {
      // No physical pages. Unmap all memory until this point.
      print << "Out of physical pages.\n";
      success = false;
    }

    // Map the physical page.
    if (success && !MapPhysicalPageAt(addr, phys, true, true, false)) {
      print << "Call to MapPhysicalPage failed.\n";
      success = false;
    }

    if (!success) {
      FreePages(start, (addr - start) / PAGE_SIZE + 1);
      return 0;
    }
  }

  return start;
}

void VirtualAddressSpace::ReleasePages(size_t addr, size_t pages) {
  if (!IsPageAlignedAddress(addr)) {
    print << "ReleaseMemory called with non page aligned address: "
          << NumberFormat::Hexidecimal << addr << '\n';
    return;
  }
  for (size_t i = 0; i < pages; i++, addr += PAGE_SIZE)
    UnmapVirtualPage(addr, /*free=*/false);
}

void VirtualAddressSpace::FreePages(size_t addr, size_t pages) {
  if (!IsPageAlignedAddress(addr)) {
    print << "FreeMemory called with non page aligned address: "
          << NumberFormat::Hexidecimal << addr << '\n';
    return;
  }

  for (size_t i = 0; i < pages; i++, addr += PAGE_SIZE)
    UnmapVirtualPage(addr, /*free=*/true);
}

size_t VirtualAddressSpace::MapPhysicalPages(size_t addr, size_t pages) {
  size_t start_virtual_address = FindAndReserveFreePageRange(pages);
  if (start_virtual_address == OUT_OF_MEMORY) return OUT_OF_MEMORY;

  for (size_t virtual_address = start_virtual_address; pages > 0;
       pages--, virtual_address += PAGE_SIZE, addr += PAGE_SIZE) {
    MapPhysicalPageAt(virtual_address, addr, false, true, false);
  }
  return start_virtual_address;
}

bool VirtualAddressSpace::MapPhysicalPageAt(size_t virtualaddr,
                                            size_t physicaladdr, bool own,
                                            bool can_write,
                                            bool throw_exception_on_access) {
  return MapPhysicalPageImpl(virtualaddr, physicaladdr,
                             TemporarilyMapPhysicalMemory, GetPhysicalPage, own,
                             can_write, throw_exception_on_access,
                             /*assign_page_table=*/false);
}

void VirtualAddressSpace::MarkAddressRangeAsFree(size_t address, size_t pages) {
  // Search for a block right before.
  FreeMemoryRange *block_before =
      free_chunks_by_address_.SearchForItemLessThanOrEqualToValue(address);

  if (block_before != nullptr) {
    if (block_before->start_address == address) {
      print << "Error: block_before->start_address == address\n";
      return;
    }
    if (block_before->start_address + (block_before->pages * PAGE_SIZE) >
        address) {
      print << "Error: block_before->start_address + (block_before->pages * "
               "PAGE_SIZE) > address\n Trying to free address "
            << NumberFormat::Hexidecimal << address << ' ';
      PrintFreeAddressRanges();
      print << "Before block: " << NumberFormat::Hexidecimal
            << block_before->start_address << " -> "
            << (block_before->start_address + (block_before->pages * PAGE_SIZE))
            << '\n';

      free_chunks_by_address_.PrintAATree();
      return;
    }

    if (block_before->start_address + (block_before->pages * PAGE_SIZE) !=
        address) {
      // The previous block doesn't touch the start of this address range.
      block_before = nullptr;
    }
  }

  // Search for a block right after.
  FreeMemoryRange *block_after =
      free_chunks_by_address_.SearchForItemEqualToValue(address +
                                                        (pages * PAGE_SIZE));

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
  size_t last_entry = pml4_;
  // Walk the page table hierarchy.
  for (int level = 0; level < kNumPageTableLevels; level++) {
    size_t *table = static_cast<size_t *>(
        TemporarilyMapPhysicalPages(last_entry & ~(PAGE_SIZE - 1), level));
    last_entry = table[CalculateIndexForAddressInPageTable(level, virtualaddr)];
    // Check that the entry is valid.
    if ((last_entry & PageTableEntryBits::kIsPresent) == 0)
      return OUT_OF_MEMORY;
  }

  // Check if the caller wants to ignore unowned pages and this entry is for an
  // unowned page.
  if (ignore_unowned_pages && (last_entry & PageTableEntryBits::kIsOwned) == 0)
    return OUT_OF_MEMORY;

  // Return the address of this entry.
  return last_entry & ~(PAGE_SIZE - 1);
}

size_t VirtualAddressSpace::GetOrCreateVirtualPage(size_t virtualaddr) {
  size_t physical_address = GetPhysicalAddress(virtualaddr,
                                               /*ignore_unowned_pages=*/false);
  if (physical_address != OUT_OF_MEMORY) return physical_address;

  physical_address = GetPhysicalPage();
  if (physical_address == OUT_OF_PHYSICAL_PAGES) return OUT_OF_MEMORY;

  // TODO: Investigate what happens if this were lazily allocated page not yet
  // allocated.
  if (!MarkVirtualAddressAsUsed(virtualaddr)) {
    FreePhysicalPage(physical_address);
    return OUT_OF_MEMORY;
  }

  if (MapPhysicalPageAt(virtualaddr, physical_address, true, true, false)) {
    return physical_address;
  } else {
    MarkAddressRangeAsFree(virtualaddr, 1);
    FreePhysicalPage(physical_address);
    return OUT_OF_MEMORY;
  }
}

void VirtualAddressSpace::PrintFreeAddressRanges() {
  print << "Free address ranges:\n" << NumberFormat::Hexidecimal;
  for (auto *fmr : free_memory_ranges_) {
    print << ' ' << fmr->start_address << "->"
          << (fmr->start_address + PAGE_SIZE * fmr->pages) << '\n';
  }
}

void VirtualAddressSpace::SetMemoryAccessRights(size_t address, size_t rights) {
  if (!IsAddressInCorrectSpace(address)) return;

  size_t last_entry = pml4_;
  size_t *table = nullptr;
  size_t last_index = 0;
  // Walk the page table hierarchy.
  for (int level = 0; level < kNumPageTableLevels; level++) {
    size_t *table = static_cast<size_t *>(
        TemporarilyMapPhysicalPages(last_entry & ~(PAGE_SIZE - 1), level));
    last_index = CalculateIndexForAddressInPageTable(level, address);
    last_entry = table[last_index];
    // Check that the entry is valid.
    if ((last_entry & PageTableEntryBits::kIsPresent) == 0) return;
  }

  // Check that the address space owns the page.
  if ((last_entry & PageTableEntryBits::kIsOwned) == 0) return;

  // Remove the bits we might be set.
  last_entry &= ~(PageTableEntryBits::kIsExecuteDisabled |
                  PageTableEntryBits::kIsWritable);

  // Set the relevant bits.
  if (rights & MemoryAccessRights::kWriteAccess)
    last_entry |= PageTableEntryBits::kIsWritable;
  if ((rights & MemoryAccessRights::kExecuteAccess) == 0)
    last_entry |= PageTableEntryBits::kIsExecuteDisabled;

  table[last_index] = last_entry;
  FlushVirtualPage(address);
}

void VirtualAddressSpace::SwitchToAddressSpace() {
  if (this != current_address_space) {
    current_address_space = this;
#ifndef __TEST__
    __asm__ __volatile__("mov %0, %%cr3" ::"b"(pml4_));
#endif
  }
}

VirtualAddressSpace &VirtualAddressSpace::CurrentAddressSpace() {
  return *current_address_space;
}

bool VirtualAddressSpace::CreateUserSpacePML4() {
  pml4_ = GetPhysicalPage();
  if (pml4_ == OUT_OF_PHYSICAL_PAGES) {
    pml4_ = OUT_OF_MEMORY;
    return false;
  }

  // Clear out this virtual address space.
  size_t *ptr = (size_t *)TemporarilyMapPhysicalPages(pml4_, 0);
  for (size_t i = 0; i < kPageTableEntries - 1; i++) ptr[i] = 0;

  // Copy the kernel's address space into this.
  size_t *kernel_ptr =
      (size_t *)TemporarilyMapPhysicalPages(KernelAddressSpace().pml4_, 1);
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
#ifndef __TEST__
    __asm__ __volatile__("hlt");
#endif
  }
}

bool VirtualAddressSpace::MarkVirtualAddressAsUsed(size_t address) {
  FreeMemoryRange *block_before =
      free_chunks_by_address_.SearchForItemLessThanOrEqualToValue(address);

  // Check if memory is already occupied.
  if (block_before == nullptr) return false;

  // Check if memory is already occupied.
  if (block_before->start_address + (block_before->pages * PAGE_SIZE) <=
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
      block_before->start_address += PAGE_SIZE;
      block_before->pages--;
      AddFreeMemoryRange(block_before);
    }
  } else if (block_before->start_address +
                 ((block_before->pages - 1) * PAGE_SIZE) ==
             address) {
    // Bump this free memory range down and re-add it.
    block_before->pages--;
    AddFreeMemoryRange(block_before);
  } else {
    // Split this free memory block into two.
    auto block_after = ObjectPool<FreeMemoryRange>::Allocate();
    if (block_after == nullptr) {
      // Out of memory, undo what we did above.
      AddFreeMemoryRange(block_before);
      return false;
    }

    // Calculate how many free pages there will be before and after this page
    // is removed.
    size_t pages_before = (address - block_before->start_address) / PAGE_SIZE;
    size_t pages_after = block_before->pages - pages_before - 1;

    block_before->pages = pages_before;
    AddFreeMemoryRange(block_before);

    block_after->start_address = address + PAGE_SIZE;
    block_after->pages = pages_after;
    AddFreeMemoryRange(block_after);
  }

  return true;
}

bool VirtualAddressSpace::MapPhysicalPageImpl(
    size_t virtualaddr, size_t physicaladdr,
    void *(*temporarily_map_physical_memory)(size_t addr, size_t index),
    size_t (*get_physical_page)(), bool own, bool can_write,
    bool throw_exception_on_access, bool assign_page_table) {
  if (!IsAddressInCorrectSpace(virtualaddr)) return false;
  bool is_kernel_address = IsKernelAddress(virtualaddr);

  // The physical addresses of the tables at each level.
  size_t table_addr[kNumPageTableLevels];
  // Whether the table at this level was allocated during this call.
  bool allocated_table[kNumPageTableLevels];
  // The mapped tables at each level.
  size_t *tables[kNumPageTableLevels];

  // Populate the highest level.
  table_addr[0] = pml4_;
  allocated_table[0] = false;
  tables[0] = (size_t *)temporarily_map_physical_memory(table_addr[0], 0);

  // Walk the page table hierarchy, creating tables as needed.
  for (int level = 0; level < kDeepestPageTableLevel; level++) {
    int index = CalculateIndexForAddressInPageTable(level, virtualaddr);
    if (assign_page_table && level == kNumPageTableLevels - 2) {
      // Mapping a page table into memory (this is used for mapping the
      // page table used for temporarily accessing memory). This gets applied
      // at the second to last level (PML2).
      size_t &entry = tables[level][index];
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
      if (new_table_physicaladdr == OUT_OF_PHYSICAL_PAGES) {
        // Deallocate any pages that were allocated this call.
        for (int level_to_deallocate = level; level_to_deallocate >= 1;
             level_to_deallocate--) {
          if (allocated_table[level_to_deallocate]) {
            // This table was allocated this call, so free it.
            FreePhysicalPage(table_addr[level_to_deallocate]);
            // Erase it in the parent table.
            int index_in_parent = CalculateIndexForAddressInPageTable(
                level_to_deallocate - 1, virtualaddr);
            tables[level_to_deallocate - 1] =
                (size_t *)temporarily_map_physical_memory(
                    new_table_physicaladdr, level_to_deallocate - 1);
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
      tables[level + 1] = (size_t *)temporarily_map_physical_memory(
          new_table_physicaladdr, level + 1);
      // Clear the new table.
      for (int i = 0; i < kPageTableEntries; i++) tables[level + 1][i] = 0;

      allocated_table[level + 1] = true;
    } else {
      // Entry is not blank, map it into memory.
      table_addr[level + 1] = tables[level][index] & ~(PAGE_SIZE - 1);
      // Map in the new table.
      tables[level + 1] = (size_t *)temporarily_map_physical_memory(
          table_addr[level + 1], level + 1);
      allocated_table[level + 1] = false;
    }
  }
  // Get the entry in the deepest page table level (PML1).
  size_t &entry =
      tables[kDeepestPageTableLevel][CalculateIndexForAddressInPageTable(
          kDeepestPageTableLevel, virtualaddr)];
  if (entry != 0 && entry != kDudPageEntry) {
    // Don't worry about cleaning up PML2/3 because for it to be mapped PML2/3
    // must already exist.
    print << "Mapping page to " << NumberFormat::Hexidecimal << virtualaddr
          << " but something is already there.\n";
    return false;
  }

  // Write the new entry in the PML1.
  entry = throw_exception_on_access
              ? kDudPageEntry
              : CreatePageTableEntry(physicaladdr, can_write,
                                     !is_kernel_address, own);

  if (this == current_address_space || is_kernel_address) {
    // We need to flush the TLB because we are either in this address space or
    // it's kernel memory (which we're always in the address space of.)
    FlushVirtualPage(virtualaddr);
  }
  return true;
}

// Unmaps a virtual page - free specifies if that page should be returned to
// the physical memory manager.
void VirtualAddressSpace::UnmapVirtualPage(size_t virtualaddr, bool free) {
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
  size_t *tables[kNumPageTableLevels];

  // Populate the highest level.
  table_addr[0] = pml4_;
  tables[0] = (size_t *)TemporarilyMapPhysicalPages(table_addr[0], 0);

  // Walk the page table hierarchy.
  for (int level = 0; level < kDeepestPageTableLevel; level++) {
    int index = CalculateIndexForAddressInPageTable(level, virtualaddr);
    // Nothing to do if the entry is blank.
    if (tables[level][index] == 0) return;
    // Entry is not blank, map it into memory.
    table_addr[level + 1] = tables[level][index] & ~(PAGE_SIZE - 1);
    // Map in the new table.
    tables[level + 1] =
        (size_t *)TemporarilyMapPhysicalPages(table_addr[level + 1], level + 1);
  }

  // Get the entry in the deepest page table level (PML1).
  size_t &entry =
      tables[kDeepestPageTableLevel][CalculateIndexForAddressInPageTable(
          kDeepestPageTableLevel, virtualaddr)];

  // Free the page if requested and if it's owned. This is optional because
  // shared memory and memory mapped IO can be unmapped without freeing the
  // physical pages.
  if (free && (entry & PageTableEntryBits::kIsOwned) != 0)
    FreePhysicalPage(entry & ~(PAGE_SIZE - 1));

  // Remove this entry for the deepest page table.
  entry = 0;
  MarkAddressRangeAsFree(virtualaddr, 1);

  if (this == current_address_space || IsKernelAddress(virtualaddr)) {
    // Flush the TLB if we are in this address space or if it's a kernel page.
    FlushVirtualPage(virtualaddr);
  }

  // Scan the page tables to see if they are completely empty so that the
  // physical pages can be released. Don't release the shallowest level (the
  // PML4).
  for (int level = kDeepestPageTableLevel; level > 0; level--) {
    // Return if there's anything still in the table.
    for (int i = 0; i < kPageTableEntries; i++) {
      if (tables[level][i] != 0) return;
    }
    // This table is empty so it can be freed.
    FreePhysicalPage(table_addr[level]);
    // Mark the entry as free in the parent table.
    tables[level - 1]
          [CalculateIndexForAddressInPageTable(level - 1, virtualaddr)] = 0;
  }
}

void VirtualAddressSpace::AddFreeMemoryRange(FreeMemoryRange *fmr) {
  if (!IsPageAlignedAddress(fmr->start_address)) {
    print << "AddFreeMemoryRange called with non page "
             "aligned "
             "address: "
          << NumberFormat::Hexidecimal << fmr->start_address << '\n';
  }
  free_chunks_by_address_.Insert(fmr);
  free_chunks_by_size_.Insert(fmr);
  free_memory_ranges_.AddFront(fmr);
}

void VirtualAddressSpace::RemoveFreeMemoryRange(FreeMemoryRange *fmr) {
  free_chunks_by_address_.Remove(fmr);
  free_chunks_by_size_.Remove(fmr);
  free_memory_ranges_.Remove(fmr);
}

bool VirtualAddressSpace::IsAddressInCorrectSpace(size_t virtualaddr) {
  bool is_kernel_address = IsKernelAddress(virtualaddr);
  bool is_kernel_address_space = this == &KernelAddressSpace();
  return is_kernel_address == is_kernel_address_space;
}
