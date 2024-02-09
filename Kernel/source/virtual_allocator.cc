#include "virtual_allocator.h"

#include "object_pools.h"
#include "physical_allocator.h"
#include "process.h"
#include "shared_memory.h"
#include "text_terminal.h"

// #define DEBUG

// Our paging structures made at boot time, these can be freed after the virtual
// allocator has been initialized.
#ifdef __TEST__
size_t Pml4[512];
size_t Pdpt[512];
size_t Pd[512];
#else
extern size_t Pml4[];
extern size_t Pdpt[];
extern size_t Pd[];
#endif

// The kernel's virtual address space.
struct VirtualAddressSpace kernel_address_space;

// The currently loaded virtual address space.
struct VirtualAddressSpace *current_address_space;

// Pointer to a page table that we use when we want to temporarily map physical
// memory.
size_t *temp_memory_page_table;
// Start address of what the temporary page table refers to.
size_t temp_memory_start;

// Start of the free memory on boot.
extern size_t bssEnd;

// The size of the page table, in bytes.
#define PAGE_TABLE_SIZE 4096  // 4 KB

// The size of a page table entry, in bytes.
#define PAGE_TABLE_ENTRY_SIZE 8

// The number of entries in a page table. Each entry is 8 bytes long.
#define PAGE_TABLE_ENTRIES (PAGE_TABLE_SIZE / PAGE_TABLE_ENTRY_SIZE)

// The highest user space address in lower half "canonical" 48-bit memory.
#define MAX_LOWERHALF_USERSPACE_ADDRESS 0x00007FFFFFFFFFFF

// The lowest user space address in higher half "canonical" 48-bit memory.
#define MIN_HIGHERHALF_USERSPACE_ADDRESS 0xFFFF800000000000

// A dud page table entry with all but the ownership and present bit set.
// We use a zeroed out entry to mean there's no page here, but this is
// actually reserved, such as for lazily allocated shared buffer.
#define DUD_PAGE_ENTRY (~(1 | (1 << 9)))

// The page sent to SetMemoryAccessRights can be written to.
#define WRITE_ACCESS 1

// The page sent to SetMemoryAccessRights can be executed.
#define EXECUTE_ACCESS 2

// Statically allocated FreeMemoryRanges added to the object pool so they can be
// allocated before the dynamic memory allocation is set up.
#define STATICALLY_ALLOCATED_FREE_MEMORY_RANGES_COUNT 2
struct FreeMemoryRange statically_allocated_free_memory_ranges
    [STATICALLY_ALLOCATED_FREE_MEMORY_RANGES_COUNT];

// Marks an address range as being free in the address space, starting at the
// address and spanning the provided number of pages.
void MarkAddressRangeAsFree(struct VirtualAddressSpace *address_space,
                            size_t address, size_t pages);

// Returns the FreeMemoryRange from a pointer to a `node_by_address` field.
struct FreeMemoryRange *FreeMemoryRangeFromNodeByAddress(
    struct AATreeNode *node) {
  size_t node_offset =
      (size_t) & ((struct FreeMemoryRange *)0)->node_by_address;
  return (struct FreeMemoryRange *)((size_t)node - node_offset);
}

// Returns the start address from a pointer to a `node_by_address` field.
size_t FreeMemoryRangeAddressFromAATreeNode(struct AATreeNode *node) {
  return FreeMemoryRangeFromNodeByAddress(node)->start_address;
}

// Returns the FreeMemoryRange from a pointer to a `node_by_size` field.
struct FreeMemoryRange *FreeMemoryRangeFromNodeBySize(struct AATreeNode *node) {
  size_t node_offset = (size_t) & ((struct FreeMemoryRange *)0)->node_by_size;

  return (struct FreeMemoryRange *)((size_t)node - node_offset);
}

// Returns the size from a pointer to a `node_by_size` field.
size_t FreeMemoryRangeSizeFromAATreeNode(struct AATreeNode *node) {
  return FreeMemoryRangeFromNodeBySize(node)->pages;
}

void VerifyAddressSpaceStructuresAreAllTheSameSize(
    struct VirtualAddressSpace *address_space) {
  int nodes_in_address_tree =
      CountNodesInAATree(&address_space->free_chunks_by_address);
  int nodes_in_size_tree =
      CountNodesInAATree(&address_space->free_chunks_by_size);
  int nodes_in_linked_list = 0;
  struct FreeMemoryRange *current_fmr = address_space->free_memory_ranges;
  while (current_fmr != nullptr) {
    nodes_in_linked_list++;
    current_fmr = current_fmr->next;
  }

  if (nodes_in_address_tree != nodes_in_size_tree ||
      nodes_in_address_tree != nodes_in_linked_list) {
    PrintString("Nodes in address tree: ");
    PrintNumber(nodes_in_address_tree);
    PrintString(" != Nodes in size tree: ");
    PrintNumber(nodes_in_size_tree);
    PrintString(" != Nodes in linked list: ");
    PrintNumber(nodes_in_linked_list);
    PrintChar('\n');
  }
}

void PrintFreeAddressRanges(struct VirtualAddressSpace *address_space) {
  PrintString("Free address ranges:\n");
  struct FreeMemoryRange *current_fmr = address_space->free_memory_ranges;
  while (current_fmr != nullptr) {
    PrintChar(' ');
    PrintHex(current_fmr->start_address);
    PrintString("->");
    PrintHex(current_fmr->start_address + PAGE_SIZE * current_fmr->pages);
    PrintChar('\n');
    current_fmr = current_fmr->next;
  }
}

void AddFreeMemoryRangeToVirtualAddressSpace(
    struct VirtualAddressSpace *address_space, struct FreeMemoryRange *fmr) {
  InsertNodeIntoAATree(&address_space->free_chunks_by_address,
                       &fmr->node_by_address,
                       FreeMemoryRangeAddressFromAATreeNode);
  InsertNodeIntoAATree(&address_space->free_chunks_by_size, &fmr->node_by_size,
                       FreeMemoryRangeSizeFromAATreeNode);
  fmr->previous = nullptr;
  fmr->next = address_space->free_memory_ranges;
  if (fmr->next != nullptr) fmr->next->previous = fmr;
  address_space->free_memory_ranges = fmr;

  VerifyAddressSpaceStructuresAreAllTheSameSize(address_space);
}

void RemoveFreeMemoryRangeFromVirtualAddressSpace(
    struct VirtualAddressSpace *address_space, struct FreeMemoryRange *fmr) {
  RemoveNodeFromAATree(&address_space->free_chunks_by_address,
                       &fmr->node_by_address,
                       FreeMemoryRangeAddressFromAATreeNode);
  RemoveNodeFromAATree(&address_space->free_chunks_by_size, &fmr->node_by_size,
                       FreeMemoryRangeSizeFromAATreeNode);

  if (fmr->previous == 0) {
    address_space->free_memory_ranges = fmr->next;
  } else {
    fmr->previous->next = fmr->next;
  }
  if (fmr->next != 0) fmr->next->previous = fmr->previous;

  VerifyAddressSpaceStructuresAreAllTheSameSize(address_space);
}

// Maps a physical address to a virtual address in the kernel - at boot time
// while paging is initializing. assign_page_table - true if we're assigning a
// page table (for our temp memory) rather than a page.
void MapKernelMemoryPreVirtualMemory(size_t virtualaddr, size_t physicaladdr,
                                     bool assign_page_table) {
  // Find the index into each PML table:
  // 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
  // 4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765
  // 4321
  //                     #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^
  //                     ^^^^ pml4       pml3       pml2       pml1        flags
  size_t pml4_entry = (virtualaddr >> 39) & 511;
  size_t pml3_entry = (virtualaddr >> 30) & 511;
  size_t pml2_entry = (virtualaddr >> 21) & 511;
  size_t pml1_entry = (virtualaddr >> 12) & 511;

  // Look in PML4.
  size_t *ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(
      kernel_address_space.pml4);
  if (pml4_entry != 511) {
    PrintString("Attempting to map kernel memory not in the last PML4 entry.");
#ifndef __TEST__
    __asm__ __volatile__("hlt");
#endif
  }
  if (ptr[pml4_entry] == 0) {
    // If this entry is blank blank, create a PML3 table.
    size_t new_pml3 = GetPhysicalPagePreVirtualMemory();

    // Clear it.
    ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(new_pml3);
    size_t i;
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
      ptr[i] = 0;
    }

    // Switch back to the PML4.
    ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(
        kernel_address_space.pml4);

    // Write it in.
    ptr[pml4_entry] = new_pml3 | 0x1;
  }

  size_t pml3 = ptr[pml4_entry] & ~(PAGE_SIZE - 1);

  // Look in PML3.
  ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml3);
  if (ptr[pml3_entry] == 0) {
    // If this entry is blank, create a PML2 table.
    size_t new_pml2 = GetPhysicalPagePreVirtualMemory();

    // Clear it.
    ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(new_pml2);
    size_t i;
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
      ptr[i] = 0;
    }

    // Switch back to the PML3.
    ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml3);

    // Write it in.
    ptr[pml3_entry] = new_pml2 | 0x1;
  }

  size_t pml2 = ptr[pml3_entry] & ~(PAGE_SIZE - 1);

  if (assign_page_table) {
    // We're assigning a page table to the PML2 rather than a page to the PML1.
    ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml2);
    size_t entry = physicaladdr | 0x1;
    ptr[pml2_entry] = entry;
    return;
  }

  // Look in PML2.
  ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml2);
  if (ptr[pml2_entry] == 0) {
    // Entry blank, create a PML1 table.
    size_t new_pml1 = GetPhysicalPagePreVirtualMemory();

    // Clear it.
    ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(new_pml1);
    size_t i;
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
      ptr[i] = 0;
    }

    // Switch back to the PML2.
    ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml2);

    // Write it in.
    ptr[pml2_entry] = new_pml1 | 0x1;
  }

  size_t pml1 = ptr[pml2_entry] & ~(PAGE_SIZE - 1);

  // Write us in PML1.
  ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml1);
  size_t entry = physicaladdr | 0x3;
  ptr[pml1_entry] = entry;
}

// An initial statically allocated FreeMemoryRange that we can use to represent
// the initial range of free memory before we can dynamically allocate memory.
struct FreeMemoryRange initial_kernel_memory_range;

// Initializes the virtual allocator.
void InitializeVirtualAllocator() {
  // We entered long mode with a temporary setup, now it's time to build a real
  // paging system for us.

  // Allocate a physical page to use as the kernel's PML4 and clear it.
  size_t kernel_pml4 = GetPhysicalPagePreVirtualMemory();
  kernel_address_space.pml4 = kernel_pml4;
  kernel_address_space.free_memory_ranges = nullptr;
  InitializeAATree(&kernel_address_space.free_chunks_by_address);
  InitializeAATree(&kernel_address_space.free_chunks_by_size);

  // Clear the PML4.
  size_t *ptr =
      (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(kernel_pml4);
  size_t i;
  for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
    ptr[i] = 0;
  }

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
  size_t page_table_range = PAGE_SIZE * PAGE_TABLE_ENTRIES;
  temp_memory_start = (i + page_table_range) & ~(page_table_range - 1);

  size_t before_temp_memory = i;

  MapKernelMemoryPreVirtualMemory(temp_memory_start,
                                  physical_temp_memory_page_table, true);

  start_of_free_kernel_memory = temp_memory_start + (2 * 1024 * 1024);

  // Set the assigned bit to each of the temporary page table entries so we
  // don't think it's free to allocate stuff into.
  //  ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(
  //      physical_temp_memory_page_table);
  //  for (i = 0; i < PAGE_TABLE_ENTRIES; i++) ptr[i] = 1;  // Assigned.

  // Hand create our first statically allocated FreeMemoryRange.
  initial_kernel_memory_range.start_address = start_of_free_kernel_memory;
  // Subtracting by 0 because the kernel lives at the top of the address space.
  initial_kernel_memory_range.pages =
      (0 - start_of_free_kernel_memory) / PAGE_SIZE;
  initial_kernel_memory_range.previous = nullptr;
  initial_kernel_memory_range.next = nullptr;

  AddFreeMemoryRangeToVirtualAddressSpace(&kernel_address_space,
                                          &initial_kernel_memory_range);

  // Flush and load the kernel's new and final PML4.
  // Set the current address space to a dud entry so SwitchToAddressSpace works.
  current_address_space = (struct VirtualAddressSpace *)nullptr;
  SwitchToAddressSpace(&kernel_address_space);

  // Add the statically allocated free memory ranges.
  for (int i = 0; i < STATICALLY_ALLOCATED_FREE_MEMORY_RANGES_COUNT; i++)
    ReleaseFreeMemoryRange(&statically_allocated_free_memory_ranges[i]);

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
  if (pml4 == OUT_OF_PHYSICAL_PAGES) {
    return OUT_OF_MEMORY;
  }

  // Clear out this virtual address space.
  size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
  size_t i;
  for (i = 0; i < PAGE_TABLE_ENTRIES - 1; i++) {
    ptr[i] = 0;
  }

  // Copy the kernel's address space into this.
  size_t *kernel_ptr =
      (size_t *)TemporarilyMapPhysicalMemory(kernel_address_space.pml4, 1);
  ptr[PAGE_TABLE_ENTRIES - 1] = kernel_ptr[PAGE_TABLE_ENTRIES - 1];

  return pml4;
}

bool InitializeVirtualAddressSpace(
    struct VirtualAddressSpace *virtual_address_space) {
  virtual_address_space->pml4 = CreateUserSpacePML4();
  if (virtual_address_space->pml4 == OUT_OF_MEMORY) return false;
  virtual_address_space->free_memory_ranges = nullptr;
  InitializeAATree(&virtual_address_space->free_chunks_by_address);
  InitializeAATree(&virtual_address_space->free_chunks_by_size);

  // Set up what memory ranges are free. x86-64 processors use 48-bit canonical
  // addresses, split into lower-half and higher-half memory.

  // First, add the lower half memory.
  struct FreeMemoryRange *fmr = AllocateFreeMemoryRange();
  if (fmr == nullptr) {
    FreePhysicalPage(virtual_address_space->pml4);
    return false;
  }

  fmr->start_address = 0;
  fmr->pages = MAX_LOWERHALF_USERSPACE_ADDRESS / PAGE_SIZE;
  AddFreeMemoryRangeToVirtualAddressSpace(virtual_address_space, fmr);

  // Now add the higher half memory.
  fmr = AllocateFreeMemoryRange();
  if (fmr != nullptr) {
    // We can gracefully continue if for some reason we couldn't allocate
    // another FreeMemoryRange.

    fmr->start_address = MIN_HIGHERHALF_USERSPACE_ADDRESS;
    // Don't go too high - the kernel lives up there.
    fmr->pages =
        (VIRTUAL_MEMORY_OFFSET - MIN_HIGHERHALF_USERSPACE_ADDRESS) / PAGE_SIZE;
    AddFreeMemoryRangeToVirtualAddressSpace(virtual_address_space, fmr);
  }
  return true;
}

// Maps a physical page so that we can access it - use this before the virtual
// allocator has been initialized.
void *TemporarilyMapPhysicalMemoryPreVirtualMemory(size_t addr) {
  // Round this down to the nearest 2MB as we use 2MB pages before we setup the
  // virtual allocator.
  size_t addr_start = addr & ~(2 * 1024 * 1024 - 1);

  size_t addr_offset = addr - addr_start;
  /*
          PrintString("Requested: ");
          PrintHex(addr);
          PrintString("\nMapping ");
          PrintHex(addr_start);
          PrintString(" Offset: ");
          PrintNumber(addr_offset);
  */

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

  /*
          PrintString(" vir: ");
          PrintHex(temp_page_boot + addr_offset);
          PrintString("\nPd[511]:");
          PrintHex(Pd[511]);
          PrintChar('\n');
  */

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

void MarkAddressRangeAsFree(struct VirtualAddressSpace *address_space,
                            size_t address, size_t pages) {
  // See if this address range can be merged into a memory block before or
  // after.

  // Search for a block right before.
  struct FreeMemoryRange *block_before = nullptr;
  struct AATreeNode *node_before = SearchForNodeLessThanOrEqualToValue(
      &address_space->free_chunks_by_address, address,
      FreeMemoryRangeAddressFromAATreeNode);

  if (node_before != nullptr) {
    block_before = FreeMemoryRangeFromNodeByAddress(node_before);
    if (block_before->start_address == address) {
      PrintString("Error: block_before->start_address == address\n");
      return;
    }
    if (block_before->start_address + (block_before->pages * PAGE_SIZE) >
        address) {
      PrintString(
          "Error: block_before->start_address + (block_before->pages * "
          "PAGE_SIZE) > address\n");
      PrintString("Trying to free address ");
      PrintHex(address);
      PrintChar(' ');
      PrintFreeAddressRanges(address_space);
      PrintString("Before block: ");
      PrintHex(block_before->start_address);
      PrintString(" -> ");
      PrintHex(block_before->start_address + (block_before->pages * PAGE_SIZE));
      PrintChar('\n');
      PrintAATree(&address_space->free_chunks_by_address,
                  FreeMemoryRangeAddressFromAATreeNode);
      return;
    }

    if (block_before->start_address + (block_before->pages * PAGE_SIZE) !=
        address) {
      // The previous block doesn't touch the start of this address range.
      block_before = nullptr;
    }
  }

  // Search for a block right after.
  struct FreeMemoryRange *block_after = nullptr;
  struct AATreeNode *node_after = SearchForNodeEqualToValue(
      &address_space->free_chunks_by_address, address + (pages * PAGE_SIZE),
      FreeMemoryRangeAddressFromAATreeNode);
  if (node_after != nullptr)
    block_after = FreeMemoryRangeFromNodeByAddress(node_after);

  if (block_before != nullptr) {
    RemoveFreeMemoryRangeFromVirtualAddressSpace(address_space, block_before);

    if (block_after != nullptr) {
      // Merge into the block before and after
      RemoveFreeMemoryRangeFromVirtualAddressSpace(address_space, block_after);
      // Expand the size of the block before.
      block_before->pages += pages + block_after->pages;
      AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_before);
      // Release the block after since it was merged in.
      ReleaseFreeMemoryRange(block_after);
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
    struct FreeMemoryRange *fmr = AllocateFreeMemoryRange();
    if (fmr == nullptr) {
      return;
    }
    fmr->start_address = address;
    fmr->pages = pages;
    AddFreeMemoryRangeToVirtualAddressSpace(address_space, fmr);
  }
}

// Finds a range of free physical pages in memory - returns the first address or
// OUT_OF_MEMORY if it can't find a fit.
size_t FindAndReserveFreePageRange(struct VirtualAddressSpace *address_space,
                                   size_t pages) {
  if (pages == 0) {
    // Too many or not enough entries.
    return OUT_OF_MEMORY;
  }

  // Find a free chunk of memory in the virutal address space that is either
  // equal to or greater than what we need.
  struct AATreeNode *node = SearchForNodeGreaterThanOrEqualToValue(
      &address_space->free_chunks_by_size, pages,
      FreeMemoryRangeSizeFromAATreeNode);
  if (node == 0) {
    // Virtual address space is full.
    return OUT_OF_MEMORY;
  }
  struct FreeMemoryRange *fmr = FreeMemoryRangeFromNodeBySize(node);
  RemoveFreeMemoryRangeFromVirtualAddressSpace(address_space, fmr);

  if (fmr->pages == pages) {
    // This is exactly the size we need! We can use this whole block.
    size_t address = fmr->start_address;

    ReleaseFreeMemoryRange(fmr);

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
bool MapPhysicalPageToVirtualPage(struct VirtualAddressSpace *address_space,
                                  size_t virtualaddr, size_t physicaladdr,
                                  bool own, bool can_write,
                                  bool throw_exception_on_access) {
  // Find the index into each PML table.
  // 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
  // 4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765
  // 4321
  //                     #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^
  //                     ^^^^ pml4       pml3       pml2       pml1        flags
  size_t pml4_entry = (virtualaddr >> 39) & 511;
  size_t pml3_entry = (virtualaddr >> 30) & 511;
  size_t pml2_entry = (virtualaddr >> 21) & 511;
  size_t pml1_entry = (virtualaddr >> 12) & 511;

  bool user_page = pml4_entry != PAGE_TABLE_ENTRIES - 1;

  if (address_space == &kernel_address_space) {
    // Kernel virtual addreses must in the highest pml4 entry.
    if (user_page) {
      PrintString("Error mapping user addresses in kernel space.\n");
      return false;
    }
  } else {
    // User space virtual addresses must be below kernel memory.
    if (!user_page) {
      PrintString("Error kernel addresses in user space.\n");
      return false;
    }
  }

  bool created_pml3, created_pml2;

  size_t pml4 = address_space->pml4;
  // Look in PML4.
  size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
  if (ptr[pml4_entry] == 0) {
    // Entry blank, create a PML3 table.
    size_t new_pml3 = GetPhysicalPage();
    if (new_pml3 == OUT_OF_PHYSICAL_PAGES) return false;  // No space for PML3.

    // Clear it.
    ptr = (size_t *)TemporarilyMapPhysicalMemory(new_pml3, 1);
    size_t i;
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
      ptr[i] = 0;
    }

    // Switch back to the PML4.
    ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);

    // Write it in.
    ptr[pml4_entry] = new_pml3 | 0x3 |
                      // Set the user bit.
                      (user_page ? (1 << 2) : 0);

    created_pml3 = true;
  } else {
    created_pml3 = false;
  }

  size_t pml3 = ptr[pml4_entry] & ~(PAGE_SIZE - 1);

  // Look in PML3.
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
  if (ptr[pml3_entry] == 0) {
    // Entry blank, create a PML2 table.
    size_t new_pml2 = GetPhysicalPage();
    if (new_pml2 == OUT_OF_PHYSICAL_PAGES) {
      // No space for pml2.
      if (created_pml3) {
        ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
        ptr[pml4_entry] = 0;
        FreePhysicalPage((size_t)pml3);
      }
      return false;
    }

    // Clear it.
    ptr = (size_t *)TemporarilyMapPhysicalMemory(new_pml2, 2);
    size_t i;
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
      ptr[i] = 0;
    }

    // Switch back to the PML3.
    ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);

    // Write it in.
    ptr[pml3_entry] = new_pml2 | 0x3 |
                      // Set the user bit.
                      (user_page ? (1 << 2) : 0);

    created_pml2 = true;
  } else {
    created_pml2 = false;
  }

  size_t pml2 = ptr[pml3_entry] & ~(PAGE_SIZE - 1);

  // Look in PML2.
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
  if (ptr[pml2_entry] == 0) {
    // Entry blank, create a PML1 table.
    size_t new_pml1 = GetPhysicalPage();
    if (new_pml1 == OUT_OF_PHYSICAL_PAGES) {
      // No space for PML1.
      if (created_pml2) {
        ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
        ptr[pml3_entry] = ((size_t)pml3);
        FreePhysicalPage((size_t)pml2);
      }

      if (created_pml3) {
        ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
        ptr[pml4_entry] = 0;
        FreePhysicalPage((size_t)pml3);
      }
      return false;
    }

    // Clear it.
    ptr = (size_t *)TemporarilyMapPhysicalMemory(new_pml1, 3);
    size_t i;
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
      ptr[i] = 0;
    }

    // Switch back to the PML2.
    ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);

    // Write it in.
    ptr[pml2_entry] = new_pml1 | 0x3 |
                      // Set the user bit.
                      (user_page ? (1 << 2) : 0);
  }

  size_t pml1 = ptr[pml2_entry] & ~(PAGE_SIZE - 1);

  // Check if this address has already been mapped in PML1.
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
  if (ptr[pml1_entry] != 0 && ptr[pml1_entry] != DUD_PAGE_ENTRY) {
    // We don't worry about cleaning up PML2/3 because for it to be mapped
    // PML2/3 must already exist.
    PrintString("Mapping page to ");
    PrintHex(virtualaddr);
    PrintString(" but something is already there.\n");
    return false;
  }

  // Write us in PML1.
  size_t entry;
  if (throw_exception_on_access) {
    entry = DUD_PAGE_ENTRY;
  } else {
    entry = physicaladdr |
            // Set the present bit.
            1 |
            // Set the write bit.
            (can_write ? (1 << 1) : 0) |
            // Set the user bit.
            (user_page ? (1 << 2) : 0) |
            // Set the ownership bit (a custom bit.)
            (own ? (1 << 9) : 0);
  }
  ptr[pml1_entry] = entry;

  if (address_space == current_address_space || !user_page) {
    // We need to flush the TLB because we are either in this address space or
    // it's kernel memory (which we're always in the address space of.)
    FlushVirtualPage(virtualaddr);
  }

  return true;
}

// Return the physical address mapped at a virtual address, returning
// OUT_OF_MEMORY if is not mapped.
size_t GetPhysicalAddress(struct VirtualAddressSpace *address_space,
                          size_t virtualaddr, bool ignore_unowned_pages) {
  size_t pml4_entry = (virtualaddr >> 39) & 511;
  size_t pml3_entry = (virtualaddr >> 30) & 511;
  size_t pml2_entry = (virtualaddr >> 21) & 511;
  size_t pml1_entry = (virtualaddr >> 12) & 511;

  if (address_space == &kernel_address_space) {
    // Kernel virtual addreses must in the last pml4 entry
    if (pml4_entry < PAGE_TABLE_ENTRIES - 1) {
      return OUT_OF_MEMORY;
    }
  } else {
    // User space virtual addresses must below kernel memory.
    if (pml4_entry == PAGE_TABLE_ENTRIES - 1) {
      return OUT_OF_MEMORY;
    }
  }

  // Look in PML4.
  size_t pml4 = address_space->pml4;
  size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
  if (ptr[pml4_entry] == 0) {
    // Entry blank.
    return OUT_OF_MEMORY;
  }

  // Look in PML3.
  size_t pml3 = ptr[pml4_entry] & ~(PAGE_SIZE - 1);
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
  if (ptr[pml3_entry] == 0) {
    // Entry blank.
    return OUT_OF_MEMORY;
  }

  // Look in PML2.
  size_t pml2 = ptr[pml3_entry] & ~(PAGE_SIZE - 1);
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);

  if (ptr[pml2_entry] == 0) {
    // Entry blank.
    return OUT_OF_MEMORY;
  }

  size_t pml1 = ptr[pml2_entry] & ~(PAGE_SIZE - 1);

  // Look in PML1.
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
  if ((ptr[pml1_entry] & 1) == 0) {
    // Page isn't present.
    return OUT_OF_MEMORY;
  } else if (ignore_unowned_pages && (ptr[pml1_entry] & (1 << 9)) == 0) {
    // We don't own this page, and we want to ignore unowned pages.
    return OUT_OF_MEMORY;
  } else {
    return ptr[pml1_entry] & 0xFFFFFFFFFFFFF000;
  }
}

bool MarkVirtualAddressAsUsed(struct VirtualAddressSpace *address_space,
                              size_t address) {
  struct AATreeNode *node_before = SearchForNodeLessThanOrEqualToValue(
      &address_space->free_chunks_by_address, address,
      FreeMemoryRangeAddressFromAATreeNode);

  if (node_before == nullptr) {
    // Memory is occupied.
    return false;
  }

  struct FreeMemoryRange *block_before =
      FreeMemoryRangeFromNodeByAddress(node_before);

  if (block_before->start_address + (block_before->pages * PAGE_SIZE) <=
      address) {
    // Memory is occupied.
    return false;
  }

  RemoveFreeMemoryRangeFromVirtualAddressSpace(address_space, block_before);

  if (block_before->start_address == address) {
    if (block_before->pages == 1) {
      // Exactly the size we need.
      ReleaseFreeMemoryRange(block_before);
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

    struct FreeMemoryRange *block_after = AllocateFreeMemoryRange();
    if (block_after == nullptr) {
      // Out of memory, undo what we did above.
      AddFreeMemoryRangeToVirtualAddressSpace(address_space, block_before);
      return false;
    }

    // Calculate how many free pages there will be before and after this page is
    // removed.
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
size_t GetOrCreateVirtualPage(struct VirtualAddressSpace *address_space,
                              size_t virtualaddr) {
  size_t physical_address = GetPhysicalAddress(address_space, virtualaddr,
                                               /*ignore_unowned_pages=*/false);
  if (physical_address != OUT_OF_MEMORY) {
    return physical_address;
  }

  physical_address = GetPhysicalPage();
  if (physical_address == OUT_OF_PHYSICAL_PAGES) {
    return OUT_OF_MEMORY;
  }

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

size_t AllocateVirtualMemoryInAddressSpace(
    struct VirtualAddressSpace *address_space, size_t pages) {
  return AllocateVirtualMemoryInAddressSpaceBelowMaxBaseAddress(
      address_space, pages, 0xFFFFFFFFFFFFFFFF);
}

size_t AllocateVirtualMemoryInAddressSpaceBelowMaxBaseAddress(
    struct VirtualAddressSpace *address_space, size_t pages,
    size_t max_base_address) {
  size_t start = FindAndReserveFreePageRange(address_space, pages);
  if (start == OUT_OF_MEMORY) {
    return 0;
  }

  // Allocate each page we've found.
  size_t addr = start;
  size_t i;
  for (i = 0; i < pages; i++, addr += PAGE_SIZE) {
    // Get a physical page.
    size_t phys = GetPhysicalPageAtOrBelowAddress(max_base_address);

    bool success = true;
    if (phys == OUT_OF_PHYSICAL_PAGES) {
      // No physical pages. Unmap all memory until this point.
      PrintString("Out of physical pages.\n");
      success = false;
    }

    // Map the physical page.
    if (success && !MapPhysicalPageToVirtualPage(address_space, addr, phys,
                                                 true, true, false)) {
      PrintString("Call to MapPhysicalPageToVirtualPage failed.\n");
      success = false;
    }

    if (!success) {
      for (; start < addr; start += PAGE_SIZE) {
        UnmapVirtualPage(address_space, start, true);
      }
      // Mark the rest as free.
      MarkAddressRangeAsFree(address_space, addr, pages - i);
      return 0;
    }
  }

  return start;
}

size_t ReleaseVirtualMemoryInAddressSpace(
    struct VirtualAddressSpace *address_space, size_t addr, size_t pages,
    bool free) {
  size_t i = 0;
  for (; i < pages; i++, addr += PAGE_SIZE) {
    UnmapVirtualPage(address_space, addr, free);
  }
}

size_t MapPhysicalMemoryInAddressSpace(
    struct VirtualAddressSpace *address_space, size_t addr, size_t pages) {
  size_t start_virtual_address =
      FindAndReserveFreePageRange(address_space, pages);
  if (start_virtual_address == OUT_OF_MEMORY) return OUT_OF_MEMORY;

  for (size_t virtual_address = start_virtual_address; pages > 0;
       pages--, virtual_address += PAGE_SIZE, addr += PAGE_SIZE) {
#ifdef DEBUG
    PrintString("Mapping ");
    PrintHex(addr);
    PrintString(" to ");
    PrintHex(virtual_address);
    PrintString("\n");
#endif
    MapPhysicalPageToVirtualPage(address_space, virtual_address, addr, false,
                                 true, false);
  }
  return start_virtual_address;
}

// Unmaps a virtual page - free specifies if that page should be returned to the
// physical memory manager.
void UnmapVirtualPage(struct VirtualAddressSpace *address_space,
                      size_t virtualaddr, bool free) {
  // Find the index into each PML table.
  // 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
  // 4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765
  // 4321
  //                     #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^
  //                     ^^^^ pml4       pml3       pml2       pml1        flags
  size_t pml4_entry = (virtualaddr >> 39) & 511;
  size_t pml3_entry = (virtualaddr >> 30) & 511;
  size_t pml2_entry = (virtualaddr >> 21) & 511;
  size_t pml1_entry = (virtualaddr >> 12) & 511;

  if (address_space == &kernel_address_space) {
    // Kernel virtual addresses must in the last PML4 entry.
    if (pml4_entry < PAGE_TABLE_ENTRIES - 1) {
      return;
    }
  } else {
    // User space virtual addresses must below kernel memory.
    if (pml4_entry == PAGE_TABLE_ENTRIES - 1) {
      return;
    }
  }

  size_t pml4 = address_space->pml4;

  // Look in PML4.
  size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(address_space->pml4, 0);
  if (ptr[pml4_entry] == 0) {
    // This address isn't mapped.
    return;
  }

  size_t pml3 = ptr[pml4_entry] & ~(PAGE_SIZE - 1);

  // Look in PML3.
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
  if (ptr[pml3_entry] == 0) {
    // This address isn't mapped.
    return;
  }

  size_t pml2 = ptr[pml3_entry] & ~(PAGE_SIZE - 1);

  // Look in PML2.
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
  if (ptr[pml2_entry] == 0) {
    // This address isn't mapped.
    return;
  }

  size_t pml1 = ptr[pml2_entry] & ~(PAGE_SIZE - 1);

  // Look in PML1.
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
  if (ptr[pml1_entry] == 0) {
    // This address isn't mapped.
    return;
  }

  // This adddress was mapped somwhere.

  // Should we free it, and it is owned by this process?
  if (free && (ptr[pml1_entry] & (1 << 9)) != 0) {
    // Return the memory to the physical allocator. This is optional because we
    // don't want to do this if it's shared or memory mapped IO.

    // Figure out the physical address that this entry points to and free it.
    size_t physicaladdr = ptr[pml1_entry] & ~(PAGE_SIZE - 1);
    FreePhysicalPage(physicaladdr);

    // Load the PML1 again incase FreePhysicalPage maps something else.
    ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
  }

  // Remove this entry from the PML1.
  ptr[pml1_entry] = 0;

  MarkAddressRangeAsFree(address_space, virtualaddr, 1);

  // Scan to see if we find anything in the PML tables. If not, we can free
  // them.
  bool found = 0;
  size_t i;
  // Scan PML1.
  for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
    if (ptr[i] != 0) {
      found = 1;
      break;
    }
  }

  if (!found) {
    // There was nothing in the PML1. We can free it.
    FreePhysicalPage(pml1);

    // Remove from the entry from the PML2.
    ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
    ptr[pml2_entry] = 0;

    // Scan the PML2.
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
      if (ptr[i] != 0) {
        found = 1;
        break;
      }
    }

    if (!found) {
      // There was nothing in the PML2. We can free it.
      FreePhysicalPage(pml2);

      // Remove the entry from the PML3.
      ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
      ptr[pml3_entry] = 0;

      // Scan the PML3.
      for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        if (ptr[i] != 0) {
          found = 1;
          break;
        }
      }

      if (!found) {
        // There was nothing in the PML3. We can free it.
        FreePhysicalPage(pml3);

        // Remove the entry from the PML4.
        ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
        ptr[pml4_entry] = 0;
      }
    }
  }

  if (address_space == current_address_space ||
      pml4_entry >= PAGE_TABLE_ENTRIES - 1) {
    // Flush the TLB if we are in this address space or if it's a kernel page.
    FlushVirtualPage(virtualaddr);
  }
}

// Frees an address space. Everything it finds will be returned to the physical
// allocator so unmap any shared memory before. Please don't pass it the
// kernel's PML4.
void FreeAddressSpace(struct VirtualAddressSpace *address_space) {
  // If we're working in this address space, switch to kernel space.
  if (current_address_space == address_space) {
    SwitchToAddressSpace(&kernel_address_space);
  }

  // Scan the lower half of PML4.
  size_t pml4 = address_space->pml4;
  size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
  size_t i;
  for (i = 0; i < PAGE_TABLE_ENTRIES - 1; i++) {
    if (ptr[i] != 0) {
      // Found a PML3.
      size_t pml3 = ptr[i] & ~(PAGE_SIZE - 1);

      // Scan the PML3.
      ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
      size_t j;
      for (j = 0; j < PAGE_TABLE_ENTRIES; j++) {
        if (ptr[j] != 0) {
          // Found a PML2.
          size_t pml2 = ptr[j] & ~(PAGE_SIZE - 1);

          // Scan the PML2.
          ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
          size_t k;
          for (k = 0; k < PAGE_TABLE_ENTRIES; k++) {
            if (ptr[k] != 0) {
              // Found a PML1.
              size_t pml1 = ptr[k] & ~(PAGE_SIZE - 1);

              // Scan the PML1.
              ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
              size_t l;
              for (l = 0; l < PAGE_TABLE_ENTRIES; l++) {
                if (ptr[l] != 0 && (ptr[l] & (1 << 9)) != 0) {
                  // We found a page, find it's physical address and free it.
                  size_t physicaladdr = ptr[l] & ~(PAGE_SIZE - 1);
                  FreePhysicalPage(physicaladdr);

                  // Make sure the PML1 is mapped in memory after calling
                  // FreePhysicalPage.
                  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
                }
              }

              // Free the PML1.
              FreePhysicalPage(pml1);

              // Make sure the PML2 is mapped in memory after calling
              // FreePhysicalPage.
              ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
            }
          }

          // Free the PML2.
          FreePhysicalPage(pml2);

          // Make sure the PML3 is mapped in memory after calling
          // FreePhysicalPage.
          ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
        }
      }

      // Free the PML3.
      FreePhysicalPage(pml3);

      // Make sure the PML4 is mapped in memory after calling FreePhysicalPage.
      ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
    }
  }

  // Free the PML4.
  FreePhysicalPage(pml4);

  // Walk through the link of FreeMemoryRange objects and release them.
  struct FreeMemoryRange *current_fmr = address_space->free_memory_ranges;
  while (current_fmr != nullptr) {
    struct FreeMemoryRange *next = current_fmr->next;
    ReleaseFreeMemoryRange(current_fmr);
    current_fmr = next;
  }
}

// Switch to a virtual address space.
void SwitchToAddressSpace(struct VirtualAddressSpace *address_space) {
  if (address_space != current_address_space) {
    current_address_space = address_space;
#ifndef __TEST__
    __asm__ __volatile__("mov %0, %%cr3" ::"b"(address_space->pml4));
#endif
  }
}

// Flush the CPU lookup for a particular virtual address.
void FlushVirtualPage(size_t addr) {
  //*SwitchToAddressSpace(current_pml4);*/

  /*
  This gives me an assembler error:
          Error: junk `(%rbp))' after expression*/
#ifndef __TEST__
  __asm__ __volatile__("invlpg (%0)" : : "b"(addr) : "memory");
#endif
}

// Maps shared memory into a process's virtual address space. Returns nullptr if
// there was an issue.
struct SharedMemoryInProcess *MapSharedMemoryIntoProcess(
    struct Process *process, struct SharedMemory *shared_memory) {
  // Find a free page range to map this shared memory into.
  size_t virtual_address = FindAndReserveFreePageRange(
      &process->virtual_address_space, shared_memory->size_in_pages);
  if (virtual_address == OUT_OF_MEMORY) {
    // No space to allocate these pages to!
    return nullptr;
  }

  struct SharedMemoryInProcess *shared_memory_in_process =
      AllocateSharedMemoryInProcess();
  if (shared_memory_in_process == nullptr) {
    // Out of memory.
    MarkAddressRangeAsFree(&process->virtual_address_space, virtual_address,
                           shared_memory->size_in_pages);
    return nullptr;
  }

#ifdef DEBUG
  PrintString("Process ");
  PrintNumber(process->pid);
  PrintString(" joined shared memory at ");
  PrintHex(virtual_address);
  PrintString("\n");
#endif

  // Increment the references to this shared memory block.
  shared_memory->processes_referencing_this_block++;

  shared_memory_in_process->shared_memory = shared_memory;
  shared_memory_in_process->process = process;
  shared_memory_in_process->virtual_address = virtual_address;
  shared_memory_in_process->references = 1;

  // Add the shared memory to our process's linked list.
  shared_memory_in_process->next_in_process = process->shared_memory;
  process->shared_memory = shared_memory_in_process;

  // Add the process to the shared memory.
  shared_memory_in_process->previous_in_shared_memory = nullptr;
  shared_memory_in_process->next_in_shared_memory =
      shared_memory->first_process;
  shared_memory->first_process = shared_memory_in_process;

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
    struct Process *process,
    struct SharedMemoryInProcess *shared_memory_in_process) {
#ifdef DEBUG
  PrintString("Process ");
  PrintNumber(process->pid);
  PrintString(" is leaving shared memory.\n");
#endif
  // TODO: Wake any threads waiting for this page. (They'll page fault, but what
  // else can we do?)

  // Unmap the virtual pages.
  ReleaseVirtualMemoryInAddressSpace(
      &process->virtual_address_space,
      shared_memory_in_process->virtual_address,
      shared_memory_in_process->shared_memory->size_in_pages, false);

  // Remove from linked list in the process.
  if (process->shared_memory == shared_memory_in_process) {
    // First element in the linked list.
    process->shared_memory = shared_memory_in_process->next_in_process;
  } else {
    // Iterate through until we find it.
    struct SharedMemoryInProcess *previous = process->shared_memory;
    while (previous != nullptr &&
           previous->next_in_process != shared_memory_in_process) {
      previous = previous->next_in_process;
    }

    if (previous == nullptr) {
      PrintString(
          "Shared memory can't be unmapped from a process that "
          "it's not mapped to.\n");
      return;
    }

    // Remove us from the linked list.
    previous->next_in_process = shared_memory_in_process->next_in_process;
  }

  // Remove from the linked list in the shared memory.
  struct SharedMemory *shared_memory = shared_memory_in_process->shared_memory;

  if (shared_memory_in_process->previous_in_shared_memory == nullptr) {
    shared_memory->first_process =
        shared_memory_in_process->next_in_shared_memory;
  } else {
    shared_memory_in_process->previous_in_shared_memory->next_in_shared_memory =
        shared_memory_in_process->next_in_shared_memory;
  }
  if (shared_memory_in_process->next_in_shared_memory != nullptr) {
    shared_memory_in_process->next_in_shared_memory->previous_in_shared_memory =
        shared_memory_in_process->previous_in_shared_memory;
  }

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

  ReleaseSharedMemoryInProcess(shared_memory_in_process);
}

void SetMemoryAccessRights(struct VirtualAddressSpace *address_space,
                           size_t address, size_t rights) {
  size_t pml4_entry = (address >> 39) & 511;
  size_t pml3_entry = (address >> 30) & 511;
  size_t pml2_entry = (address >> 21) & 511;
  size_t pml1_entry = (address >> 12) & 511;

  if (address_space == &kernel_address_space) {
    // Kernel virtual addreses must in the last pml4 entry
    if (pml4_entry < PAGE_TABLE_ENTRIES - 1) {
      return;
    }
  } else {
    // User space virtual addresses must below kernel memory.
    if (pml4_entry == PAGE_TABLE_ENTRIES - 1) {
      return;
    }
  }

  // Look in PML4.
  size_t pml4 = address_space->pml4;
  size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
  if (ptr[pml4_entry] == 0) {
    // Entry blank.
    return;
  }

  // Look in PML3.
  size_t pml3 = ptr[pml4_entry] & ~(PAGE_SIZE - 1);
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
  if (ptr[pml3_entry] == 0) {
    // Entry blank.
    return;
  }

  // Look in PML2.
  size_t pml2 = ptr[pml3_entry] & ~(PAGE_SIZE - 1);
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);

  if (ptr[pml2_entry] == 0) {
    // Entry blank.
    return;
  }

  size_t pml1 = ptr[pml2_entry] & ~(PAGE_SIZE - 1);

  // Look in PML1.
  ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
  if ((ptr[pml1_entry] & 1) == 0) {
    // Page isn't present.
    return;
  } else if ((ptr[pml1_entry] & (1 << 9)) == 0) {
    // We don't own this page.
    return;
  } else {
    size_t execute_disabled_bit = 1L << 63L;
    size_t write_bit = 1L << 1L;

    size_t entry = ptr[pml1_entry];
    // Remove the bits we might set.
    entry &= ~(execute_disabled_bit | write_bit);

    // Set any bits we want.
    if (rights & WRITE_ACCESS) entry |= write_bit;
    if ((rights & EXECUTE_ACCESS) == 0) entry |= execute_disabled_bit;

    ptr[pml1_entry] = entry;
    FlushVirtualPage(address);
  }
}
