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
#pragma once

#include "aa_tree.h"
#include "enum.h"
#include "linked_list.h"
#include "types.h"

// Bits passed to SetMemoryAccessRights.
namespace MemoryAccessRights {

// The page can be written to.
constexpr int kWriteAccess = 1;

// The page can be executed.
constexpr int kExecuteAccess = 2;

}  // namespace MemoryAccessRights

// Represents a virtual address space for a process.
class VirtualAddressSpace {
 public:
  ~VirtualAddressSpace();

  // Initializes this virtual address space for user space.
  bool InitializeUserSpace();

  // Initializes this virtual address space for the kernel.
  void InitializeKernelSpace(size_t start_of_free_kernel_memory_at_boot,
                             size_t &temp_memory_start,
                             size_t *&temp_memory_page_table);

  // Finds a range of free physical pages in memory - returns the first address
  // or OUT_OF_MEMORY if it can't find a fit.
  size_t FindAndReserveFreePageRange(size_t pages);

  // Reserves a range of address page in a virtual address space, only if all
  // the pages within this range are currently free.
  bool ReserveAddressRange(size_t address, size_t pages);

  size_t AllocatePages(size_t pages);

  size_t AllocatePagesBelowMaxBaseAddress(size_t pages,
                                          size_t max_base_address);

  // Releases virtual memory in the address space, but does not free the
  // underlying physical pages.
  void ReleasePages(size_t addr, size_t pages);

  // Free virtual memory in address space.
  void FreePages(size_t addr, size_t pages);

  // Maps phyical memory into an address space. Returns the virtual address of
  // where it is mapped.
  size_t MapPhysicalPages(size_t addr, size_t pages);

  // Maps a physical page to a virtual page. Make sure the address range has
  // already been reserved. Returns if it was successful.
  bool MapPhysicalPageAt(size_t virtualaddr, size_t physicaladdr, bool own,
                         bool can_write, bool throw_exception_on_access);

  // Marks an address range as being free in the address space, starting at the
  // address and spanning the provided number of pages.
  void MarkAddressRangeAsFree(size_t address, size_t pages);

  // Return the physical address mapped at a virtual address, returning
  // OUT_OF_MEMORY if is not mapped.
  size_t GetPhysicalAddress(size_t virtualaddr, bool ignore_unowned_pages);

  // Gets or creates a virtual page in an address space, returning the physical
  // address or OUT_OF_MEMORY if it fails.
  size_t GetOrCreateVirtualPage(size_t virtualaddr);

  // Prints the ranges of unallocated addresses.
  void PrintFreeAddressRanges();

  // Sets the access rights of a memory page, if the process owns the page. The
  // rights are a bitfield:
  //  Bit 0: The memory can be written to.
  //  Bit 1: The memory can be executed.
  void SetMemoryAccessRights(size_t address, size_t rights);

  // Switch to a virtual address space. Remember to call this if allocating or
  // freeing pages to flush the changes!
  void SwitchToAddressSpace();

  static VirtualAddressSpace &CurrentAddressSpace();

  // Whether this is the kernel's address space.
  bool IsKernelAddressSpace();

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

 private:
  // Creates a user's sapce virtual address space.
  bool CreateUserSpacePML4();

  // Maps a physical address to a virtual address in the kernel - at boot time
  // while paging is initializing. assign_page_table - true if we're assigning a
  // page table (for our temp memory) rather than a page.
  void MapKernelMemoryPreVirtualMemory(size_t virtualaddr, size_t physicaladdr,
                                       bool assign_page_table);

  bool MarkVirtualAddressAsUsed(size_t address);

  bool MapPhysicalPageImpl(
      size_t virtualaddr, size_t physicaladdr,
      void *(*temporarily_map_physical_memory)(size_t addr, size_t index),
      size_t (*get_physical_page)(), bool own, bool can_write,
      bool throw_exception_on_access, bool assign_page_table);

  void UnmapVirtualPage(size_t virtualaddr, bool free);

  void AddFreeMemoryRange(FreeMemoryRange *fmr);

  void RemoveFreeMemoryRange(FreeMemoryRange *fmr);

  bool IsAddressInCorrectSpace(size_t virtualaddr);

  // Physical address of the PML4 for this virtual address space.
  size_t pml4_;

  // Linked list of free memory ranges.
  LinkedList<FreeMemoryRange, &FreeMemoryRange::node> free_memory_ranges_;

  // Tree of free chunks by start address.
  AATree<FreeMemoryRange, &FreeMemoryRange::node_by_address,
         &FreeMemoryRange::start_address>
      free_chunks_by_address_;

  // Tree of free chunks by size.
  AATree<FreeMemoryRange, &FreeMemoryRange::node_by_size,
         &FreeMemoryRange::pages>
      free_chunks_by_size_;
};
