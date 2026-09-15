#ifndef TEST
#include "memory/physical_allocator.h"

#include "../../../third_party/multiboot2.h"
#include "hardware/io.h"
#include "containers/object_pool.h"
#include "containers/object_pools.h"
#include "containers/spinlock.h"
#include "output/text_terminal.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"

// Start of the free memory on boot.
extern size_t bssEnd;

namespace memory {

using containers::CleanUpObjectPools;
using containers::InterruptSafeSpinlock;
using containers::InterruptSafeSpinlockGuard;
using output::NumberFormat;
using output::print;

namespace {

InterruptSafeSpinlock g_physical_allocator_spinlock;

// Bits of a free-list entry that hold the physical address. The upper bits are
// status flags that the CPU and the page tables use, and are not part of the
// address.
constexpr size_t kPhysicalAddressMask = 0x000FFFFFFFFFFFFFL;

// Physical memory is divided into 4kb pages. A linked stack of them is kept,
// allowing pages to be popped off and pushed onto it. This pointer points to
// the top of the stack (next free page), and the first thing in that page will
// be a pointer to the next page.
size_t g_next_free_page_address;

// Extracts the page address from an entry on the free page stack. The entry
// must be compared against kOutOfPhysicalPages *before* being passed in, since
// masking destroys the sentinel.
size_t PageAddressOfStackEntry(size_t entry) {
  return entry & kPhysicalAddressMask & ~(kPageSize - 1);
}

// Before virtual memory is set up, the temporary paging system set up in
// boot.asm only associates the maps the first 8MB of physical memory into
// virtual memory. The multiboot structure can be quite huge (especially if
// there are multi-boot modules passed in to the bootloader), and so the
// multiboot data might extend past this 8MB boundary. The
// SafeReadUint32/SafeReadUint64 functions make sure the physical memory is
// temporarily mapped into virtual memory before reading it. This only works if
// the values are sure not to cross the 2MB page boundaries (which they
// shouldn't).
uint32 SafeReadUint32(uint32 *value) {
  return *(volatile uint32 *)TemporarilyMapPhysicalMemoryPreVirtualMemory((size_t)value,
                                                                 0);
}

// 64-bit equivalent to SafeReadUint32.
uint64 SafeReadUint64(uint64 *value) {
  return *(volatile uint64 *)TemporarilyMapPhysicalMemoryPreVirtualMemory((size_t)value,
                                                                 0);
}

// Calculates the start of the free memory at boot.
void CalculateStartOfFreeMemoryAtBoot() {
  g_start_of_free_memory_at_boot = (size_t)&bssEnd;

  uint32 mb_addr = SafeReadUint32(&MultibootInfo.addr);
  uint32 mb_total_size = SafeReadUint32((uint32 *)mb_addr);
  if ((size_t)mb_addr + mb_total_size > g_start_of_free_memory_at_boot)
    g_start_of_free_memory_at_boot = (size_t)mb_addr + mb_total_size;

  // Loop through each of the tags in the multiboot.
  multiboot_tag *tag;
  for (tag = (multiboot_tag *)(size_t)(mb_addr + 8);
       SafeReadUint32(&tag->type) != MULTIBOOT_TAG_TYPE_END;
       tag =
           (multiboot_tag *)((size_t)tag +
                             (size_t)((SafeReadUint32(&tag->size) + 7) & ~7))) {
    // Make sure there's enough space for this tag.
    size_t tag_end = (size_t)tag + SafeReadUint32(&tag->size);
    if (tag_end > g_start_of_free_memory_at_boot)
      g_start_of_free_memory_at_boot = tag_end;

    if (SafeReadUint32(&tag->type) == MULTIBOOT_TAG_TYPE_MODULE) {
      auto *module_tag = (multiboot_tag_module *)tag;
      uint32 mod_start = SafeReadUint32(&module_tag->mod_start);
      uint32 mod_end = SafeReadUint32(&module_tag->mod_end);

      // If this is a multiboot module, ensure enough space to fit it.
      if (mod_end > g_start_of_free_memory_at_boot)
        g_start_of_free_memory_at_boot = mod_end;
    }
  }

  size_t end_tag_end = (size_t)tag + 8;
  if (end_tag_end > g_start_of_free_memory_at_boot)
    g_start_of_free_memory_at_boot = end_tag_end;

  g_start_of_free_memory_at_boot =
      RoundUpToPageAlignedAddress(g_start_of_free_memory_at_boot);
}

}  // namespace

// The total number of bytes of system memory.
size_t g_total_system_memory;

// The total number of free pages.
size_t g_free_pages;

// The end of multiboot memory. This is memory that is temporarily reserved to
// hold the multiboot information put there by the bootloader, and will be
// released after calling DoneWithMultibootMemory.
size_t g_start_of_free_memory_at_boot;

void InitializePhysicalAllocator() {
  g_total_system_memory = 0;
  g_free_pages = 0;
  CalculateStartOfFreeMemoryAtBoot();

  // Initialize the stack to kOutOfPhysicalPages, then pages will be pushed
  // onto the stack As
  g_next_free_page_address = kOutOfPhysicalPages;

  // The multiboot bootloader (GRUB) already did the hard work of asking the
  // BIOS what physical memory is available. The bootloader puts this
  // information into the multiboot header.

  // Loop through each of the tags in the multiboot.
  multiboot_tag *tag;
  for (tag = (multiboot_tag *)(size_t)(SafeReadUint32(&MultibootInfo.addr) + 8);
       SafeReadUint32(&tag->type) != MULTIBOOT_TAG_TYPE_END;
       tag =
           (multiboot_tag *)((size_t)tag +
                             (size_t)((SafeReadUint32(&tag->size) + 7) & ~7))) {
    uint32 size = SafeReadUint32(&tag->size);
    // If a tag has size 0 (invalid) and is not the END tag, it implies a
    // corrupted multiboot structure or an issue with SafeReadUint32.
    // Continuing to parse with size 0 would lead to an infinite loop in tag
    // advancement.
    if (size == 0 && SafeReadUint32(&tag->type) != MULTIBOOT_TAG_TYPE_END) {
      print << "Error: Multiboot tag with size 0 encountered at "
            << NumberFormat::Hexidecimal << (size_t)tag
            << ". Stopping multiboot parse.\n";
      break;
    }

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

        if (SafeReadUint32(&mmap->type) == MULTIBOOT_MEMORY_AVAILABLE) {
          // This memory is avaliable for usage (in contrast to memory that is
          // reserved, dead, etc.)

          size_t start = SafeReadUint64(&mmap->addr);
          size_t end = RoundDownToPageAlignedAddress(start + len);

          // Make sure this is free memory past the kernel.
          if (start < g_start_of_free_memory_at_boot)
            start = g_start_of_free_memory_at_boot;

          start = RoundUpToPageAlignedAddress(start);

          // Now divide this memory up into pages and iterate through
          // them.
          size_t page_addr;
          for (page_addr = start; page_addr < end; page_addr += kPageSize) {
            // Push this page onto the linked stack.

            // Map this physical memory, so can write the previous stack page to
            // it.
            size_t *bp = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(
                page_addr, 0);

            // Write the previous stack head to the start of this page.
            *bp = g_next_free_page_address;
            // Sets the stack head to this page.
            g_next_free_page_address = page_addr;

            g_free_pages++;
            g_total_system_memory += kPageSize;
          }
        }
      }
    }
  }
}

void DoneWithMultibootMemory() {
  // Frees the memory pages between the end of kernel memory
  size_t end_of_kernel_memory = (size_t)&bssEnd;
  size_t start = RoundUpToPageAlignedAddress(end_of_kernel_memory);
  size_t end = g_start_of_free_memory_at_boot;

  if (!IsPageAlignedAddress(start) || !IsPageAlignedAddress(end)) {
    print << "DoneWithMultibootMemory not page aligned: "
          << NumberFormat::Hexidecimal << start << " -> " << end << '\n';
  }

  for (size_t page = start; page < end; page += kPageSize) {
    KernelAddressSpace().FreePages(page + kVirtualMemoryOffset, 1);
    // FreePages adds these to the free list, but they were never counted while
    // walking the multiboot memory map, so without this the reported free
    // memory can exceed the reported total.
    __atomic_fetch_add(&g_total_system_memory, kPageSize, __ATOMIC_RELAXED);
  }
}

size_t GetPhysicalPagePreVirtualMemory() {
  if (g_next_free_page_address == kOutOfPhysicalPages) {
    // No more free pages.
    return kOutOfPhysicalPages;
  }
  // Take the top page from the stack.
  size_t addr = PageAddressOfStackEntry(g_next_free_page_address);
  // Pop it from the stack by mapping the page to physical memory so the
  // pointer to the next free page can be grabbed.
  size_t *bp = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(addr, 0);
  g_next_free_page_address = *bp;

  g_free_pages--;

  return addr;
}

size_t GetPhysicalPage() {
  return GetPhysicalPageAtOrBelowAddress(0xFFFFFFFFFFFFFFFF);
}

size_t GetPhysicalPageAtOrBelowAddress(size_t max_base_address) {
  bool cleaned_up = false;
retry:
  size_t addr = kOutOfPhysicalPages;
  size_t* bp = nullptr;
  {
    InterruptSafeSpinlockGuard guard(g_physical_allocator_spinlock);
    if (g_next_free_page_address == kOutOfPhysicalPages) {
      if (cleaned_up) return kOutOfPhysicalPages;
      goto need_cleanup;
    }

    // Take the top page from the stack.
    addr = PageAddressOfStackEntry(g_next_free_page_address);

    if (addr <= max_base_address) {
      // The first address was sufficient. This should be the most common use
      // case, except for drivers that need a low physical memory address for
      // DMA.

      // Pop it from the stack by mapping the page to physical memory so the
      // pointer to the next free page can be grabbed.
      bp = (size_t*)TemporarilyMapPhysicalPages(addr, 5);
      g_next_free_page_address = *bp;
    } else {
      // Keep walking the stack of free pages until one is found that's below
      // the max base address. The previous address needs to be remembered so
      // the pointer in the stack can be updated to skip over the page taken
      // out.
      size_t* previous_bp = nullptr;
      do {
        // Walk to the next page. The stored entry is read unmasked, because
        // masking it first would destroy the end-of-list sentinel.
        previous_bp = (size_t*)TemporarilyMapPhysicalPages(addr, 5);
        size_t next_entry = *previous_bp;

        if (next_entry == kOutOfPhysicalPages) {
          if (!cleaned_up) goto need_cleanup;
          return kOutOfPhysicalPages;
        }

        addr = PageAddressOfStackEntry(next_entry);
      } while (addr > max_base_address);
      // Loop while current addr is too high.

      bp = (size_t*)TemporarilyMapPhysicalPages(addr, 6);

      // Update the previous page to skip over this page.
      *previous_bp = *bp;
    }

    g_free_pages--;

    // Clear out the page, so nothing is leaked from another process. Done
    // inside the guard because `bp` points at a per-core temporary mapping slot
    // that the allocator itself re-maps.
    memset((char*)bp, 0, kPageSize);
  }

  return addr;

need_cleanup:
  CleanUpObjectPools();
  cleaned_up = true;
  goto retry;
}


void FreePhysicalPage(size_t addr) {
  // Reject the failure sentinels before masking, which would otherwise turn
  // them into plausible looking addresses and splice garbage into the stack.
  if (addr == kOutOfPhysicalPages || addr == kOutOfMemory || addr == kError)
    return;

  // Mask off flags, status bits (e.g. Bit 63), and alignment bits.
  addr &= kPhysicalAddressMask & ~(kPageSize - 1);
  if (addr == 0) return;

  InterruptSafeSpinlockGuard guard(g_physical_allocator_spinlock);

  // Push this page onto the linked stack.

  // Map this physical memory, so the previous stack page can be written to it.
  size_t *bp = (size_t *)TemporarilyMapPhysicalPages(addr, 5);
  // Write the previous stack head to the start of this page.
  *bp = g_next_free_page_address;

  // Sets the stack head to this page.
  g_next_free_page_address = addr;

  g_free_pages++;
}

bool IsPageAlignedAddress(size_t address) { return (address % kPageSize) == 0; }

size_t RoundDownToPageAlignedAddress(size_t address) {
  return address - (address % kPageSize);
}

}  // namespace memory

#endif // TEST
