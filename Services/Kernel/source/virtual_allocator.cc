#include "virtual_allocator.h"

#include "object_pool.h"
#include "physical_allocator.h"
#include "process.h"
#include "shared_memory.h"
#include "text_terminal.h"

// Our paging structures made at boot time, these can be freed after the virtual
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

// The size of the page table, in bytes.
constexpr size_t kPageTableSize = 4096;  // 4 KB

// The size of a page table entry, in bytes.
constexpr size_t kPageTableEntrySize = 8;

// The number of entries in a page table. Each entry is 8 bytes long.
constexpr size_t kPageTableEntries = kPageTableSize / kPageTableEntrySize;

// The highest user space address in lower half "canonical" 48-bit memory.
constexpr size_t kMaxLowerHalfUserSpaceAddress = 0x00007FFFFFFFFFFF;

// The lowest user space address in higher half "canonical" 48-bit memory.
constexpr size_t kMinHigherHalfUserSpaceAddress = 0xFFFF800000000000;

// Page table entries:

// Pointer to a page table that we use when we want to temporarily map physical
// memory.
size_t *temp_memory_page_table;
// Start address of what the temporary page table refers to.
size_t temp_memory_start;

// Start of the free memory on boot.
extern size_t bssEnd;

// A dud page table entry with all but the ownership and present bit set.
// We use a zeroed out entry to mean there's no page here, but this is
// actually reserved, such as for lazily allocated shared buffer.
constexpr size_t kDudPageEntry = (~(1 | (1 << 9)));

// Bits passed to SetMemoryAccessRights.
namespace MemoryAccessRights {

// The page can be written to.
constexpr int kWriteAccess = 1;

// The page can be executed.
constexpr int kExecuteAccess = 2;

}  // namespace MemoryAccessRights

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

// Statically allocated FreeMemoryRanges added to the object pool so they can be
// allocated before the dynamic memory allocation is set up.
constexpr int kStaticallyAllocatedFreeMemoryRangesCount = 2;
FreeMemoryRange statically_allocated_free_memory_ranges
    [kStaticallyAllocatedFreeMemoryRangesCount];

// Marks an address range as being free in the address space, starting at the
// address and spanning the provided number of pages.
void MarkAddressRangeAsFree(VirtualAddressSpace *address_space, size_t address,
                            size_t pages);

void PrintFreeAddressRanges(VirtualAddressSpace *address_space) {
  print << "Free address ranges:\n" << NumberFormat::Hexidecimal;
  for (auto *fmr : address_space->free_memory_ranges) {
    print << ' ' << fmr->start_address << "->"
          << (fmr->start_address + PAGE_SIZE * fmr->pages) << '\n';
  }
}

void AddFreeMemoryRangeToVirtualAddressSpace(VirtualAddressSpace *address_space,
                                             FreeMemoryRange *fmr) {
  address_space->free_chunks_by_address.Insert(fmr);
  address_space->free_chunks_by_size.Insert(fmr);
  address_space->free_memory_ranges.AddFront(fmr);
}

void RemoveFreeMemoryRangeFromVirtualAddressSpace(
    VirtualAddressSpace *address_space, FreeMemoryRange *fmr) {
  address_space->free_chunks_by_address.Remove(fmr);
  address_space->free_chunks_by_size.Remove(fmr);
  address_space->free_memory_ranges.Remove(fmr);
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

bool IsAddressInCorrectSpace(VirtualAddressSpace *address_space,
                             size_t virtualaddr) {
  bool is_kernel_address = IsKernelAddress(virtualaddr);
  bool is_kernel_address_space = address_space == &kernel_address_space;
  return is_kernel_address == is_kernel_address_space;
}

// Maps a physical address to a virtual address.
inline bool MapPhysicalPageToVirtualPageImpl(
    VirtualAddressSpace *address_space, size_t virtualaddr, size_t physicaladdr,
    void *(*temporarily_map_physical_memory)(size_t addr, size_t index),
    size_t (*get_physical_page)(), bool own, bool can_write,
    bool throw_exception_on_access, bool assign_page_table) {
  if (!IsAddressInCorrectSpace(address_space, virtualaddr)) return false;
  bool is_kernel_address = IsKernelAddress(virtualaddr);

  // The physical addresses of the tables at each level.
  size_t table_addr[kNumPageTableLevels];
  // Whether the table at this level was allocated during this call.
  bool allocated_table[kNumPageTableLevels];
  // The mapped tables at each level.
  size_t *tables[kNumPageTableLevels];

  // Populate the highest level.
  table_addr[0] = address_space->pml4;
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
        print << "Attempting to map page table to a location where there's "
                 "already a page table..\n";
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

  // Write the new entry in the in ePML1.
  entry = throw_exception_on_access
              ? kDudPageEntry
              : CreatePageTableEntry(physicaladdr, can_write,
                                     !is_kernel_address, own);

  if (address_space == current_address_space || is_kernel_address) {
    // We need to flush the TLB because we are either in this address space or
    // it's kernel memory (which we're always in the address space of.)
    FlushVirtualPage(virtualaddr);
  }
  return true;
}

// Maps a physical address to a virtual address in the kernel - at boot time
// while paging is initializing. assign_page_table - true if we're assigning a
// page table (for our temp memory) rather than a page.
void MapKernelMemoryPreVirtualMemory(size_t virtualaddr, size_t physicaladdr,
                                     bool assign_page_table) {
  if (!MapPhysicalPageToVirtualPageImpl(
          &kernel_address_space, virtualaddr, physicaladdr,
          TemporarilyMapPhysicalMemoryPreVirtualMemory,
          GetPhysicalPagePreVirtualMemory,
          /*own=*/true, /*can_write=*/true, /*throw_exception_on_access=*/false,
          assign_page_table)) {
    print << "Out of memory during kernel initialization.\n";
#ifndef __TEST__
    __asm__ __volatile__("hlt");
#endif
  }
}

void MarkAddressRangeAsFree(VirtualAddressSpace *address_space, size_t address,
                            size_t pages) {
  // See if this address range can be merged into a memory block before or
  // after.

  // Search for a block right before.
  FreeMemoryRange *block_before =
      address_space->free_chunks_by_address.SearchForItemLessThanOrEqualToValue(
          address);

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
      PrintFreeAddressRanges(address_space);
      print << "Before block: " << NumberFormat::Hexidecimal
            << block_before->start_address << " -> "
            << (block_before->start_address + (block_before->pages * PAGE_SIZE))
            << '\n';

      address_space->free_chunks_by_address.PrintAATree();
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
      address_space->free_chunks_by_address.SearchForItemEqualToValue(
          address + (pages * PAGE_SIZE));

  if (block_before != nullptr) {
    RemoveFreeMemoryRangeFromVirtualAddressSpace(address_space, block_before);

    if (block_after != nullptr) {
      // Merge into the block before and after
      RemoveFreeMemoryRangeFromVirtualAddressSpace(address_space, block_after);
      // Expand the size of the block before.
      block_before->pages += pages + block_after->pages;
      AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_before);
      // Release the block after since it was merged in.
      ObjectPool<FreeMemoryRange>::Release(block_after);
    } else {
      // Merge into the block before.
      // Expand the size of the block before.
      block_before->pages += pages;
      AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_before);
    }
  } else if (block_after != nullptr) {
    // Merge into the block after.
    RemoveFreeMemoryRangeFromVirtualAddressSpace(address_space, block_after);
    // Expand and pull back the size of the block after.
    block_after->start_address = address;
    block_after->pages += pages;
    AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_after);

  } else {
    // Stand alone free memory range that can't merge into anything.
    auto fmr = ObjectPool<FreeMemoryRange>::Allocate();
    if (fmr == nullptr) {
      return;
    }
    fmr->start_address = address;
    fmr->pages = pages;
    AddFreeMemoryRangeToVirtualAddressSpace(address_space, fmr);
  }
}

// An initial statically allocated FreeMemoryRange that we can use to represent
// the initial range of free memory before we can dynamically allocate memory.
FreeMemoryRange initial_kernel_memory_range;

inline void ScanAndFreePagesInLevel(size_t table_address, int level) {
  bool is_shallowest_level = level == 0;
  bool is_deepest_level = level == kDeepestPageTableLevel;
  size_t *table = (size_t *)TemporarilyMapPhysicalMemory(table_address, level);

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

}  // namespace

// The kernel's virtual address space.
VirtualAddressSpace kernel_address_space;

// The currently loaded virtual address space.
VirtualAddressSpace *current_address_space;

// Initializes the virtual allocator.
void InitializeVirtualAllocator() {
  // We entered long mode with a temporary setup, now it's time to build a real
  // paging system for us.

  // Allocate a physical page to use as the kernel's PML4 and clear it.
  size_t kernel_pml4 = GetPhysicalPagePreVirtualMemory();
  new (&kernel_address_space) VirtualAddressSpace();
  kernel_address_space.pml4 = kernel_pml4;

  // Clear the PML4.
  size_t *ptr =
      (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(kernel_pml4, 0);
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

  // Set the assigned bit to each of the temporary page table entries so we
  // don't think it's free to allocate stuff into.
  //  ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(
  //      physical_temp_memory_page_table, 0);
  //  for (i = 0; i < kPageTableEntries; i++) ptr[i] = 1;  // Assigned.

  // Hand create our first statically allocated FreeMemoryRange.
  initial_kernel_memory_range.start_address = start_of_free_kernel_memory;
  // Subtracting by 0 because the kernel lives at the top of the address space.
  initial_kernel_memory_range.pages =
      (0 - start_of_free_kernel_memory) / PAGE_SIZE;

  AddFreeMemoryRangeToVirtualAddressSpace(&kernel_address_space,
                                          &initial_kernel_memory_range);

  // Flush and load the kernel's new and final PML4.
  // Set the current address space to a dud entry so SwitchToAddressSpace works.
  current_address_space = (VirtualAddressSpace *)nullptr;
  SwitchToAddressSpace(&kernel_address_space);

  // Add the statically allocated free memory ranges.
  for (int i = 0; i < kStaticallyAllocatedFreeMemoryRangesCount; i++)
    ObjectPool<FreeMemoryRange>::Release(
        &statically_allocated_free_memory_ranges[i]);

// Reclaim the PML4, PDPT, PD set up at boot time.
#ifndef __TEST__
  UnmapVirtualPage(&kernel_address_space, (size_t)&Pml4 + VIRTUAL_MEMORY_OFFSET,
                   true);
  UnmapVirtualPage(&kernel_address_space, (size_t)&Pdpt + VIRTUAL_MEMORY_OFFSET,
                   true);
  UnmapVirtualPage(&kernel_address_space, (size_t)&Pd + VIRTUAL_MEMORY_OFFSET,
                   true);
#endif

  if (before_temp_memory < temp_memory_start) {
    // The virtual address space had to be rounded up to align with 2MB for the
    // temporary page table. This is a free range.
    size_t num_pages = (temp_memory_start - before_temp_memory) / PAGE_SIZE;
    MarkAddressRangeAsFree(&kernel_address_space, before_temp_memory,
                           num_pages);
  }
}

// Creates a user's sapce virtual address space, returns the PML4. Returns
// OUT_OF_MEMORY if it fails.
size_t CreateUserSpacePML4() {
  size_t pml4 = GetPhysicalPage();
  if (pml4 == OUT_OF_PHYSICAL_PAGES) return OUT_OF_MEMORY;

  // Clear out this virtual address space.
  size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
  for (size_t i = 0; i < kPageTableEntries - 1; i++) ptr[i] = 0;

  // Copy the kernel's address space into this.
  size_t *kernel_ptr =
      (size_t *)TemporarilyMapPhysicalMemory(kernel_address_space.pml4, 1);
  ptr[kPageTableEntries - 1] = kernel_ptr[kPageTableEntries - 1];

  return pml4;
}

bool InitializeVirtualAddressSpace(VirtualAddressSpace *virtual_address_space) {
  new (virtual_address_space) VirtualAddressSpace();
  virtual_address_space->pml4 = CreateUserSpacePML4();
  if (virtual_address_space->pml4 == OUT_OF_MEMORY) return false;

  // Set up what memory ranges are free. x86-64 processors use 48-bit canonical
  // addresses, split into lower-half and higher-half memory.

  // First, add the lower half memory.
  auto fmr = ObjectPool<FreeMemoryRange>::Allocate();
  if (fmr == nullptr) {
    FreePhysicalPage(virtual_address_space->pml4);
    return false;
  }

  fmr->start_address = 0;
  fmr->pages = kMaxLowerHalfUserSpaceAddress / PAGE_SIZE;
  AddFreeMemoryRangeToVirtualAddressSpace(virtual_address_space, fmr);

  // Now add the higher half memory.
  fmr = ObjectPool<FreeMemoryRange>::Allocate();
  if (fmr != nullptr) {
    // We can gracefully continue if for some reason we couldn't allocate
    // another FreeMemoryRange.

    fmr->start_address = kMinHigherHalfUserSpaceAddress;
    // Don't go too high - the kernel lives up there.
    fmr->pages =
        (VIRTUAL_MEMORY_OFFSET - kMinHigherHalfUserSpaceAddress) / PAGE_SIZE;
    AddFreeMemoryRangeToVirtualAddressSpace(virtual_address_space, fmr);
  }
  return true;
}

// Maps a physical page so that we can access it - use this before the virtual
// allocator has been initialized.
void *TemporarilyMapPhysicalMemoryPreVirtualMemory(size_t addr, size_t index) {
  // Round this down to the nearest 2MB as we use 2MB pages before we setup the
  // virtual allocator.
  size_t addr_start = addr & ~(2 * 1024 * 1024 - 1);
  size_t addr_offset = addr - addr_start;
  size_t entry = addr_start | 0x83;

  // Check if it different to what is currently loaded.
  if (Pd[511] != entry) {
    // Map this to the last page of our page directory we set up at boot time.
    Pd[511] = entry;

    // Flush our page table cache.
    FlushVirtualPage(addr_start);
  }

  // The virtual address of the temp page: 1GB - 2MB.
  size_t temp_page_boot = 1022 * 1024 * 1024;

  // Return a pointer to the virtual address of the requested physical memory.
  return (void *)(temp_page_boot + addr_offset);
}

// Temporarily maps physical memory (page aligned) into virtual memory so we can
// fiddle with it. index is from 0 to 511 - mapping a different address to the
// same index unmaps the previous page mapped there.
void *TemporarilyMapPhysicalMemory(size_t addr, size_t index) {
  size_t entry = addr | 0x3;

  // Check if it's not already mapped.
  if (temp_memory_page_table[index] != entry) {
    // Map this page into our temporary page table.
    temp_memory_page_table[index] = entry;
// Flush our page table cache.
#ifndef __TEST__
    __asm__ __volatile__("mov %0, %%cr3" ::"a"(current_address_space->pml4));
#endif
  }

  // Return a pointer to the virtual address of the requested physical memory.
  return (void *)(temp_memory_start + PAGE_SIZE * index);
}

// Finds a range of free physical pages in memory - returns the first address or
// OUT_OF_MEMORY if it can't find a fit.
size_t FindAndReserveFreePageRange(VirtualAddressSpace *address_space,
                                   size_t pages) {
  if (pages == 0) {
    // Too many or not enough entries.
    return OUT_OF_MEMORY;
  }

  // Find a free chunk of memory in the virutal address space that is either
  // equal to or greater than what we need.
  FreeMemoryRange *fmr =
      address_space->free_chunks_by_size.SearchForItemGreaterThanOrEqualToValue(
          pages);
  if (fmr == nullptr) return OUT_OF_MEMORY;  // Virtual address space is full.

  RemoveFreeMemoryRangeFromVirtualAddressSpace(address_space, fmr);
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

    AddFreeMemoryRangeToVirtualAddressSpace(address_space, fmr);
    return address;
  }
}

// Maps a physical page to a virtual page. Returns if it was successful.
bool MapPhysicalPageToVirtualPage(VirtualAddressSpace *address_space,
                                  size_t virtualaddr, size_t physicaladdr,
                                  bool own, bool can_write,
                                  bool throw_exception_on_access) {
  return MapPhysicalPageToVirtualPageImpl(
      address_space, virtualaddr, physicaladdr, TemporarilyMapPhysicalMemory,
      GetPhysicalPage, own, can_write, throw_exception_on_access,
      /*assign_page_table=*/false);
}

// Return the physical address mapped at a virtual address, returning
// OUT_OF_MEMORY if is not mapped.
size_t GetPhysicalAddress(VirtualAddressSpace *address_space,
                          size_t virtualaddr, bool ignore_unowned_pages) {
  size_t last_entry = address_space->pml4;
  // Walk the page table hierarchy.
  for (int level = 0; level < kNumPageTableLevels; level++) {
    size_t *table = static_cast<size_t *>(
        TemporarilyMapPhysicalMemory(last_entry & ~(PAGE_SIZE - 1), level));
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

bool MarkVirtualAddressAsUsed(VirtualAddressSpace *address_space,
                              size_t address) {
  FreeMemoryRange *block_before =
      address_space->free_chunks_by_address.SearchForItemLessThanOrEqualToValue(
          address);

  // Check if memory is already occupied.
  if (block_before == nullptr) return false;

  // Check if memory is already occupied.
  if (block_before->start_address + (block_before->pages * PAGE_SIZE) <=
      address)
    return false;

  RemoveFreeMemoryRangeFromVirtualAddressSpace(address_space, block_before);

  if (block_before->start_address == address) {
    if (block_before->pages == 1) {
      // Exactly the size we need.
      ObjectPool<FreeMemoryRange>::Release(block_before);
    } else {
      // Bump up this free memory range and re-add it.
      block_before->start_address += PAGE_SIZE;
      block_before->pages--;
      AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_before);
    }
  } else if (block_before->start_address +
                 ((block_before->pages - 1) * PAGE_SIZE) ==
             address) {
    // Bump this free memory range down and re-add it.
    block_before->pages--;
    AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_before);
  } else {
    // Split this free memory block into two.
    auto block_after = ObjectPool<FreeMemoryRange>::Allocate();
    if (block_after == nullptr) {
      // Out of memory, undo what we did above.
      AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_before);
      return false;
    }

    // Calculate how many free pages there will be before and after this page
    // is removed.
    size_t pages_before = (address - block_before->start_address) / PAGE_SIZE;
    size_t pages_after = block_before->pages - pages_before - 1;

    block_before->pages = pages_before;
    AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_before);

    block_after->start_address = address + PAGE_SIZE;
    block_after->pages = pages_after;
    AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_after);
  }

  return true;
}

// Return the physical address mapped at a virtual address, returning
// OUT_OF_MEMORY if is not mapped.
size_t GetOrCreateVirtualPage(VirtualAddressSpace *address_space,
                              size_t virtualaddr) {
  size_t physical_address = GetPhysicalAddress(address_space, virtualaddr,
                                               /*ignore_unowned_pages=*/false);
  if (physical_address != OUT_OF_MEMORY) return physical_address;

  physical_address = GetPhysicalPage();
  if (physical_address == OUT_OF_PHYSICAL_PAGES) return OUT_OF_MEMORY;

  // TODO: Investigate what happens if this were lazily allocated page not yet
  // allocated.
  if (!MarkVirtualAddressAsUsed(address_space, virtualaddr)) {
    FreePhysicalPage(physical_address);
    return OUT_OF_MEMORY;
  }

  if (MapPhysicalPageToVirtualPage(address_space, virtualaddr, physical_address,
                                   true, true, false)) {
    return physical_address;
  } else {
    MarkAddressRangeAsFree(address_space, virtualaddr, 1);
    FreePhysicalPage(physical_address);
    return OUT_OF_MEMORY;
  }
}

size_t AllocateVirtualMemoryInAddressSpace(VirtualAddressSpace *address_space,
                                           size_t pages) {
  return AllocateVirtualMemoryInAddressSpaceBelowMaxBaseAddress(
      address_space, pages, 0xFFFFFFFFFFFFFFFF);
}

size_t AllocateVirtualMemoryInAddressSpaceBelowMaxBaseAddress(
    VirtualAddressSpace *address_space, size_t pages, size_t max_base_address) {
  size_t start = FindAndReserveFreePageRange(address_space, pages);
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
    if (success && !MapPhysicalPageToVirtualPage(address_space, addr, phys,
                                                 true, true, false)) {
      print << "Call to MapPhysicalPageToVirtualPage failed.\n";
      success = false;
    }

    if (!success) {
      for (; start < addr; start += PAGE_SIZE)
        UnmapVirtualPage(address_space, start, true);
      // Mark the rest as free.
      MarkAddressRangeAsFree(address_space, addr, pages - i);
      return 0;
    }
  }

  return start;
}

void ReleaseVirtualMemoryInAddressSpace(VirtualAddressSpace *address_space,
                                        size_t addr, size_t pages, bool free) {
  size_t i = 0;
  for (; i < pages; i++, addr += PAGE_SIZE)
    UnmapVirtualPage(address_space, addr, free);
}

size_t MapPhysicalMemoryInAddressSpace(VirtualAddressSpace *address_space,
                                       size_t addr, size_t pages) {
  size_t start_virtual_address =
      FindAndReserveFreePageRange(address_space, pages);
  if (start_virtual_address == OUT_OF_MEMORY) return OUT_OF_MEMORY;

  for (size_t virtual_address = start_virtual_address; pages > 0;
       pages--, virtual_address += PAGE_SIZE, addr += PAGE_SIZE) {
    MapPhysicalPageToVirtualPage(address_space, virtual_address, addr, false,
                                 true, false);
  }
  return start_virtual_address;
}

// Unmaps a virtual page - free specifies if that page should be returned to
// the physical memory manager.
void UnmapVirtualPage(VirtualAddressSpace *address_space, size_t virtualaddr,
                      bool free) {
  if (!IsAddressInCorrectSpace(address_space, virtualaddr)) return;

  // The physical addresses of the tables at each level.
  size_t table_addr[kNumPageTableLevels];
  // Whether the table at this level was allocated during this call.
  bool allocated_table[kNumPageTableLevels];
  // The mapped tables at each level.
  size_t *tables[kNumPageTableLevels];

  // Populate the highest level.
  table_addr[0] = address_space->pml4;
  tables[0] = (size_t *)TemporarilyMapPhysicalMemory(table_addr[0], 0);

  // Walk the page table hierarchy.
  for (int level = 0; level < kDeepestPageTableLevel; level++) {
    int index = CalculateIndexForAddressInPageTable(level, virtualaddr);
    // Nothing to do if the entry is blank.
    if (tables[level][index] == 0) return;
    // Entry is not blank, map it into memory.
    table_addr[level + 1] = tables[level][index] & ~(PAGE_SIZE - 1);
    // Map in the new table.
    tables[level + 1] = (size_t *)TemporarilyMapPhysicalMemory(
        table_addr[level + 1], level + 1);
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
  MarkAddressRangeAsFree(address_space, virtualaddr, 1);

  if (address_space == current_address_space || IsKernelAddress(virtualaddr)) {
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

// Frees an address space. Everything it finds will be returned to the
// physical allocator so unmap any shared memory before. Please don't pass it
// the kernel's PML4.
void FreeAddressSpace(VirtualAddressSpace *address_space) {
  // Switch to kernel space so the address space being freed isn't active.
  if (current_address_space == address_space)
    SwitchToAddressSpace(&kernel_address_space);

  // Free the memory pages owned by the address space and all of the tables.
  ScanAndFreePagesInLevel(address_space->pml4, 0);
  FreePhysicalPage(address_space->pml4);

  // Walk through the link of FreeMemoryRange objects and release them.

  while (auto fmr = address_space->free_memory_ranges.PopFront())
    ObjectPool<FreeMemoryRange>::Release(fmr);
}

// Switch to a virtual address space.
void SwitchToAddressSpace(VirtualAddressSpace *address_space) {
  if (address_space != current_address_space) {
    current_address_space = address_space;
#ifndef __TEST__
    __asm__ __volatile__("mov %0, %%cr3" ::"b"(address_space->pml4));
#endif
  }
}

// Flush the CPU lookup for a particular virtual address.
void FlushVirtualPage(size_t addr) {
#ifndef __TEST__
  __asm__ __volatile__("invlpg (%0)" : : "b"(addr) : "memory");
#endif
}

// Maps shared memory into a process's virtual address space. Returns nullptr
// if there was an issue.
SharedMemoryInProcess *MapSharedMemoryIntoProcess(Process *process,
                                                  SharedMemory *shared_memory) {
  // Find a free page range to map this shared memory into.
  size_t virtual_address = FindAndReserveFreePageRange(
      &process->virtual_address_space, shared_memory->size_in_pages);
  if (virtual_address == OUT_OF_MEMORY) {
    // No space to allocate these pages to!
    return nullptr;
  }

  auto shared_memory_in_process = ObjectPool<SharedMemoryInProcess>::Allocate();
  if (shared_memory_in_process == nullptr) {
    // Out of memory.
    MarkAddressRangeAsFree(&process->virtual_address_space, virtual_address,
                           shared_memory->size_in_pages);
    return nullptr;
  }

  // Increment the references to this shared memory block.
  shared_memory->processes_referencing_this_block++;

  shared_memory_in_process->shared_memory = shared_memory;
  shared_memory_in_process->process = process;
  shared_memory_in_process->virtual_address = virtual_address;
  shared_memory_in_process->references = 1;

  // Add the shared memory to our process's linked list.
  process->joined_shared_memories.AddBack(shared_memory_in_process);

  // Add the process to the shared memory.
  shared_memory->joined_processes.AddBack(shared_memory_in_process);

  bool can_write = CanProcessWriteToSharedMemory(process, shared_memory);

  // Map the physical pages into memory.
  for (size_t page = 0; page < shared_memory->size_in_pages; page++) {
    // Map the physical page to the virtual address.
    if (shared_memory->physical_pages[page] == OUT_OF_PHYSICAL_PAGES) {
      MapPhysicalPageToVirtualPage(&process->virtual_address_space,
                                   virtual_address, 0, false, false, true);
    } else {
      MapPhysicalPageToVirtualPage(
          &process->virtual_address_space, virtual_address,
          shared_memory->physical_pages[page], false, can_write, false);
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
  // TODO: Wake any threads waiting for this page. (They'll page fault, but
  // what else can we do?)
  auto *process = shared_memory_in_process->process;
  auto *shared_memory = shared_memory_in_process->shared_memory;

  // Unmap the virtual pages.
  ReleaseVirtualMemoryInAddressSpace(&process->virtual_address_space,
                                     shared_memory_in_process->virtual_address,
                                     shared_memory->size_in_pages, false);

  process->joined_shared_memories.Remove(shared_memory_in_process);
  shared_memory->joined_processes.Remove(shared_memory_in_process);

  // Decrement the references to this shared memory block.
  shared_memory->processes_referencing_this_block--;
  if (shared_memory->processes_referencing_this_block == 0) {
    // There are no more refernces to this shared memory block, so we can
    // release the memory.
    ReleaseSharedMemoryBlock(shared_memory);
  } else if (process->pid == shared_memory->creator_pid &&
             (shared_memory->flags & SM_LAZILY_ALLOCATED) != 0) {
    // We are unmapping lazily allocated shared memory from the creator.
    // We'll create the pages of any threads that are sleeping because they're
    // waiting for pages to be created.
    // TODO
  }

  ObjectPool<SharedMemoryInProcess>::Release(shared_memory_in_process);
}

void SetMemoryAccessRights(VirtualAddressSpace *address_space, size_t address,
                           size_t rights) {
  if (!IsAddressInCorrectSpace(address_space, address)) return;

  size_t last_entry = address_space->pml4;
  size_t *table = nullptr;
  size_t last_index = 0;
  // Walk the page table hierarchy.
  for (int level = 0; level < kNumPageTableLevels; level++) {
    size_t *table = static_cast<size_t *>(
        TemporarilyMapPhysicalMemory(last_entry & ~(PAGE_SIZE - 1), level));
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

bool IsKernelAddress(size_t address) {
  return address >= VIRTUAL_MEMORY_OFFSET;
}

void GetUserspaceVirtualMemoryHole(size_t &hole_start_address,
                                   size_t &hole_end_address) {
  hole_start_address = kMaxLowerHalfUserSpaceAddress + 1;
  hole_end_address = kMinHigherHalfUserSpaceAddress - 1;
}

size_t PagesThatContainBytes(size_t bytes) {
  return (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
}

bool CopyKernelMemoryIntoProcess(size_t from_start, size_t to_start,
                                 size_t to_end, Process *process) {
  VirtualAddressSpace *address_space = &process->virtual_address_space;

  // The process's memory is mapped into pages. We'll copy page by page.
  size_t to_first_page = to_start & ~(PAGE_SIZE - 1);  // Round down.
  size_t to_last_page =
      (to_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  // Round up.

  size_t to_page = to_first_page;
  for (; to_page < to_last_page; to_page += PAGE_SIZE) {
    size_t physical_page_address =
        GetOrCreateVirtualPage(address_space, to_page);
    if (physical_page_address == OUT_OF_MEMORY) {
      // We ran out of memory trying to allocate the virtual page.
      return false;
    }

    size_t temp_addr =
        (size_t)TemporarilyMapPhysicalMemory(physical_page_address, 5);

    // Indices where to start/finish copying within the page.
    size_t offset_in_page_to_start_copying_at =
        to_start > to_page ? to_start - to_page : 0;
    size_t offset_in_page_to_finish_copying_at =
        to_page + PAGE_SIZE > to_end ? to_end - to_page : PAGE_SIZE;
    size_t copy_length = offset_in_page_to_finish_copying_at -
                         offset_in_page_to_start_copying_at;

    memcpy((char *)(temp_addr + offset_in_page_to_start_copying_at),
           (char *)(from_start), copy_length);

    from_start += copy_length;
  }

  return true;
}
