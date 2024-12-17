#include "physical_allocator.h"

#include "../../../third_party/multiboot2.h"
#include "io.h"
#include "object_pool.h"
#include "object_pools.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

// Start of the free memory on boot.
#ifdef __TEST__
size_t bssEnd = 0;
#else
extern size_t bssEnd;
#endif

namespace {

#ifdef __TEST__
multiboot_info MultibootInfo;
#endif

// Physical memory is divided into 4kb pages. We keep a linked stack of them
// that we can pop a page off of and push a page onto. This pointer points to
// the top of the stack (next free page), and the first thing in that page will
// be a pointer to the next page.
size_t next_free_page_address;

// Before virtual memory is set up, the temporary paging system we set up in
// boot.asm only associates the maps the first 8MB of physical memory into
// virtual memory. The multiboot structure can be quite huge (especially if
// there are multi-boot modules passed in to the bootloader), and so the
// multiboot data might extend past this 8MB boundary. The
// SafeReadUint32/SafeReadUint64 functions make sure the physical memory is
// temporarily mapped into virtual memory before reading it. This only works if
// the values are sure not to cross the 2MB page boundaries (which they
// shouldn't).
}
uint32 SafeReadUint32(uint32 *value) {
  return *(uint32 *)TemporarilyMapPhysicalMemoryPreVirtualMemory((size_t)value,
                                                                 0);
}

// 64-bit equivalent to SafeReadUint32.
uint64 SafeReadUint64(uint64 *value) {
  return *(uint64 *)TemporarilyMapPhysicalMemoryPreVirtualMemory((size_t)value,
                                                                 0);
}

// Calculates the start of the free memory at boot.
void CalculateStartOfFreeMemoryAtBoot() {
  start_of_free_memory_at_boot = (size_t)&bssEnd;

  // Loop through each of the tags in the multiboot.
  multiboot_tag *tag;
  for (tag = (multiboot_tag *)(size_t)(SafeReadUint32(&MultibootInfo.addr) + 8);
       SafeReadUint32(&tag->type) != MULTIBOOT_TAG_TYPE_END;
       tag =
           (multiboot_tag *)((size_t)tag +
                             (size_t)((SafeReadUint32(&tag->size) + 7) & ~7))) {
    // Make sure we're enough to fit in this tag.
    size_t tag_end = (size_t)tag + SafeReadUint32(&tag->size);
    if (tag_end > start_of_free_memory_at_boot) {
      start_of_free_memory_at_boot = tag_end;
    }

    if (SafeReadUint32(&tag->type) == MULTIBOOT_TAG_TYPE_MODULE) {
      multiboot_tag_module *module_tag = (multiboot_tag_module *)tag;
      uint32 mod_end = SafeReadUint32(&module_tag->mod_end);

      // If this is a multiboot module, make sure we're enough to fit
      // it in.
      if (mod_end > start_of_free_memory_at_boot) {
        start_of_free_memory_at_boot = mod_end;
      }
    }
  }

  // Round up to the nearest whole page.
  start_of_free_memory_at_boot =
      (start_of_free_memory_at_boot + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

}  // namespace

// The total number of bytes of system memory.
size_t total_system_memory;

// The total number of free pages.
size_t free_pages;

// The end of multiboot memory. This is memory that is temporarily reserved to
// hold the multiboot information put there by the bootloader, and will be
// released after calling DoneWithMultibootMemory.
size_t start_of_free_memory_at_boot;

void InitializePhysicalAllocator() {
  total_system_memory = 0;
  free_pages = 0;
  CalculateStartOfFreeMemoryAtBoot();

  // Initialize the stack to OUT_OF_PHYSICAL_PAGES, then we will push pages onto
  // the stack As
  next_free_page_address = OUT_OF_PHYSICAL_PAGES;

  // The multiboot bootloader (GRUB) already did the hard work of asking the
  // BIOS what physical memory is available. The bootloader puts this
  // information into the multiboot header.

  // Loop through each of the tags in the multiboot.
  multiboot_tag *tag;
  for (tag = (multiboot_tag *)(size_t)(MultibootInfo.addr + 8);
       SafeReadUint32(&tag->type) != MULTIBOOT_TAG_TYPE_END;
       tag =
           (multiboot_tag *)((size_t)tag +
                             (size_t)((SafeReadUint32(&tag->size) + 7) & ~7))) {
    uint32 size = SafeReadUint32(&tag->size);
    // Skip empty tags.
    if (size == 0) return;

    uint16 type = SafeReadUint32(&tag->type);
    if (type == MULTIBOOT_TAG_TYPE_MMAP) {
      // This is a memory map tag!
      multiboot_tag_mmap *mmap_tag = (multiboot_tag_mmap *)tag;

      // Iterate over each entry in the memory map.
      multiboot_mmap_entry *mmap;
      for (mmap = mmap_tag->entries; (size_t)mmap < (size_t)tag + size;
           mmap = (multiboot_mmap_entry *)((size_t)mmap +
                                           (size_t)SafeReadUint32(
                                               &mmap_tag->entry_size))) {
        uint64 len = SafeReadUint64(&mmap->len);
        total_system_memory += len;

        if (SafeReadUint32(&mmap->type) == MULTIBOOT_MEMORY_AVAILABLE) {
          // This memory is avaliable for usage (in contrast to memory that is
          // reserved, dead, etc.)

          size_t start = SafeReadUint64(&mmap->addr);
          size_t end =
              (start + len) & ~(PAGE_SIZE - 1);  // Round down to page size.

          // Make sure this is free memory past the kernel.
          if (start < start_of_free_memory_at_boot)
            start = start_of_free_memory_at_boot;

          start = (start + PAGE_SIZE - 1) &
                  ~(PAGE_SIZE - 1);  // Round up to page size.

          // Now we will divide this memory up into pages and iterate through
          // them.
          size_t page_addr;
          for (page_addr = start; page_addr < end; page_addr += PAGE_SIZE) {
            // Push this page onto the linked stack.

            // Map this physical memory, so can write the previous stack page to
            // it.
            size_t *bp = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(
                page_addr, 0);

            // Write the previous stack head to the start of this page.
            *bp = next_free_page_address;
            // Sets the stack head to this page.
            next_free_page_address = page_addr;

            free_pages++;
          }
        }
      }
    }
  }
}

// Indicates that we are done with the multiboot memory and that it can be
// released.
void DoneWithMultibootMemory() {
  // Frees the memory pages between the end of kernel memory
  size_t end_of_kernel_memory = (size_t)&bssEnd;
  size_t start =
      (end_of_kernel_memory + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  // Round up.
  size_t end = start_of_free_memory_at_boot;  // (start_of_free_memory_at_boot +
                                              // PAGE_SIZE - 1) & ~(PAGE_SIZE -
                                              // 1); // Round up.

  if (!IsPageAlignedAddress(start) || !IsPageAlignedAddress(end)) {
    print << "DoneWithMultibootMemory not page aligned: "
          << NumberFormat::Hexidecimal << start << " -> " << end << '\n';
  }

  for (size_t page = start; page < end; page += PAGE_SIZE)
    UnmapVirtualPage(&kernel_address_space, page + VIRTUAL_MEMORY_OFFSET);
}

// Grabs the next physical page (at boot time before the virtual memory
// allocator is initialized), returns OUT_OF_PHYSICAL_PAGES if there are no more
// physical pages.
size_t GetPhysicalPagePreVirtualMemory() {
  if (next_free_page_address == OUT_OF_PHYSICAL_PAGES) {
    // No more free pages.
    return OUT_OF_PHYSICAL_PAGES;
  }

  // Take the top page from the stack.
  size_t addr = next_free_page_address;

  // Pop it from the stack by mapping the page to physical memory so we can grab
  // the pointer to the next free page.
  size_t *bp = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(addr, 0);
  next_free_page_address = *bp;

  free_pages--;

  return addr;
}

// Grabs the next physical page, returns OUT_OF_PHYSICAL_PAGES if there are no
// more physical pages.
size_t GetPhysicalPage() {
  return GetPhysicalPageAtOrBelowAddress(0xFFFFFFFFFFFFFFFF);
}

size_t GetPhysicalPageAtOrBelowAddress(size_t max_base_address) {
  if (next_free_page_address == OUT_OF_PHYSICAL_PAGES) {
    // Ran out of memory. Try to clean up some memory.
    CleanUpObjectPools();

    if (next_free_page_address == OUT_OF_PHYSICAL_PAGES) {
      // No more free pages.
      return OUT_OF_PHYSICAL_PAGES;
    }
  }

  // Take the top page from the stack.
  size_t addr = next_free_page_address;

  size_t *bp;

  if (addr <= max_base_address) {
    // The first address was sufficient. This should be the most common use case
    // except for drivers that a low physical memor address for DMA.

    // Pop it from the stack by mapping the page to physical memory so we can
    // grab the pointer to the next free page.
    bp = (size_t *)TemporarilyMapPhysicalMemory(addr, 5);
    next_free_page_address = *bp;
  } else {
    // Keep walking the stack of free pages until we find one that's below the
    // max base address.

    // We need to remember the previous address so we can update the pointer in
    // the stack to skip over the page we took out.
    size_t previous_addr;
    size_t *previous_bp;
    do {
      // Walk to the next page.
      previous_addr = addr;
      previous_bp = (size_t *)TemporarilyMapPhysicalMemory(addr, 5);
      addr = *previous_bp;

      if (addr == OUT_OF_PHYSICAL_PAGES) {
        // We've reached the end of the stack.
        return OUT_OF_PHYSICAL_PAGES;
      }
    } while (addr <= max_base_address);

    bp = (size_t *)TemporarilyMapPhysicalMemory(addr, 6);

    // Update the previous page to skip over this page.
    *previous_bp = *bp;
  }

  // Clear out the page, so we don't leak anything from another process.
  memset((char *)bp, 0, PAGE_SIZE);

  free_pages--;

  return addr;
}

// Frees a physical page.
void FreePhysicalPage(size_t addr) {
  // Push this page onto the linked stack.

  // Map this physical memory, so can write the previous stack page to it.
  size_t *bp = (size_t *)TemporarilyMapPhysicalMemory(addr, 5);
  // Write the previous stack head to the start of this page.
  *bp = next_free_page_address;

  // Sets the stack head to this page.
  next_free_page_address = addr;

  free_pages++;
}

bool IsPageAlignedAddress(size_t address) { return (address % PAGE_SIZE) == 0; }

size_t RoundDownToPageAlignedAddress(size_t address) {
  return address - (address % PAGE_SIZE);
}
