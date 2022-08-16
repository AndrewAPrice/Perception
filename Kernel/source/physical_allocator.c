#include "physical_allocator.h"

#include "io.h"
#include "../../third_party/multiboot2.h"
#include "object_pools.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

// #define DEBUG

// The total number of bytes of system memory.
size_t total_system_memory;

// The total number of free pages.
size_t free_pages;

// Start of the free memory on boot.
extern size_t bssEnd;

// Physical memory is divided into 4kb pages. We keep a linked stack of them that we can pop a page off
// of and push a page onto. This pointer points to the top of the stack (next free page), and the first
// thing in that page will be a pointer to the next page.
size_t next_free_page_address;

// The end of multiboot memory. This is memory that is temporarily reserved to hold the multiboot
// information put there by the bootloader, and will be released after calling DoneWithMultibootMemory.
size_t start_of_free_memory_at_boot;

// Before virtual memory is set up, the temporary paging system we set up in
// boot.asm only associates the maps the first 8MB of physical memory into
// virtual memory. The multiboot structure can be quite huge (especially if
// there are multi-boot modules passed in to the bootloader), and so the
// multiboot data might extend past this 8MB boundary. The SafeReadUint8/
// SafeReadUint32/SafeReadUint64 functions make sure the physical memory is
// temporarily mapped into virtual memory before reading it. This only works if
// the values are sure not to cross the 2MB page boundaries (which they
// shouldn't).
uint32 SafeReadUint8(uint8* value) {
	return *(uint8*)TemporarilyMapPhysicalMemoryPreVirtualMemory((size_t)value);
}
uint32 SafeReadUint32(uint32* value) {
	return *(uint32*)TemporarilyMapPhysicalMemoryPreVirtualMemory((size_t)value);
}

// 64-bit equivalent to SafeReadUint32.
uint64 SafeReadUint64(uint64* value) {
	return *(uint64*)TemporarilyMapPhysicalMemoryPreVirtualMemory((size_t)value);
}

// Calculates the start of the free memory at boot.
void CalculateStartOfFreeMemoryAtBoot() {
	start_of_free_memory_at_boot = (size_t)&bssEnd;

	// Loop through each of the tags in the multiboot.
	struct multiboot_tag *tag;
	for(tag = (struct multiboot_tag *)(size_t)(
		SafeReadUint32(&MultibootInfo.addr) + 8);
		SafeReadUint32(&tag->type) != MULTIBOOT_TAG_TYPE_END;
		tag = (struct multiboot_tag *)((size_t) tag + (size_t)((
			SafeReadUint32(&tag->size) + 7) & ~7))) {
		// Make sure we're enough to fit in this tag.
		size_t tag_end = (size_t)tag + SafeReadUint32(&tag->size);
		if (tag_end > start_of_free_memory_at_boot) {
			start_of_free_memory_at_boot = tag_end;
		}

		if (SafeReadUint32(&tag->type) == MULTIBOOT_TAG_TYPE_MODULE) {
			struct multiboot_tag_module *module_tag = (struct multiboot_tag_module *)tag;
			uint32 mod_end = SafeReadUint32(&module_tag->mod_end);

			// If this is a multiboot module, make sure we're enough to fit
			// it in.
			if (mod_end > start_of_free_memory_at_boot) {
				start_of_free_memory_at_boot = mod_end;
			}
		}
	}

	// Round up to the nearest whole page.
	start_of_free_memory_at_boot = (start_of_free_memory_at_boot + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	
 #ifdef DEBUG
	PrintString("End of kernel: ");
	PrintHex((size_t)&bssEnd);
	PrintString(" Start of free memory: ");
	PrintHex(start_of_free_memory_at_boot);
	PrintChar('\n');
 #endif
}

void InitializePhysicalAllocator() {
	total_system_memory = 0;
	free_pages = 0;
	CalculateStartOfFreeMemoryAtBoot();

	// Initialize the stack to OUT_OF_PHYSICAL_PAGES, then we will push pages onto the stack As
	next_free_page_address = OUT_OF_PHYSICAL_PAGES;

	// The multiboot bootloader (GRUB) already did the hard work of asking the BIOS what physical
	// memory is available. The bootloader puts this information into the multiboot header.

	// Loop through each of the tags in the multiboot.
	struct multiboot_tag *tag;
	for(tag = (struct multiboot_tag *)(size_t)(MultibootInfo.addr + 8);
		SafeReadUint32(&tag->type) != MULTIBOOT_TAG_TYPE_END;
		tag = (struct multiboot_tag *)((size_t) tag + (size_t)((
			SafeReadUint32(&tag->size) + 7) & ~7))) {
		
		uint32 size = SafeReadUint32(&tag->size);
		// Skip empty tags.
		if(size == 0)
			return;

		uint16 type = SafeReadUint32(&tag->type);
		if(type == MULTIBOOT_TAG_TYPE_MMAP) {
			// This is a memory map tag!
			struct multiboot_tag_mmap *mmap_tag = (struct multiboot_tag_mmap *)tag;


			#ifdef DEBUG
				PrintString("Entry size: ");
				PrintNumber(SafeReadUint32(&mmap_tag->entry_size));
				PrintString(" Tag size: ");
				PrintNumber(size);
				PrintString(" Entries: ");
				PrintNumber(size / SafeReadUint32(&mmap_tag->entry_size));
				PrintString("\n");
			#endif

			// Iterate over each entry in the memory map.
			struct multiboot_mmap_entry *mmap;
			for(mmap = mmap_tag->entries;
				(size_t)mmap < (size_t)tag + size;
				mmap = (struct multiboot_mmap_entry *)((size_t)mmap +
					(size_t)SafeReadUint32(&mmap_tag->entry_size))) {

				#ifdef DEBUG
					PrintString("Base: ");
					PrintHex(SafeReadUint64(&mmap->addr));
					PrintString(" Length: ");
					PrintNumber(SafeReadUint64(&mmap->len));
					PrintString(" Type: ");
					PrintNumber(SafeReadUint32(&mmap->type));
					PrintChar('\n');
				#endif

				uint64 len = SafeReadUint64(&mmap->len);
				total_system_memory += len;

				if(SafeReadUint32(&mmap->type) == MULTIBOOT_MEMORY_AVAILABLE) {
					// This memory is avaliable for usage (in contrast to memory that is reserved, dead, etc.)
					
					size_t start = SafeReadUint64(&mmap->addr);
					size_t end = (start + len) & ~(PAGE_SIZE - 1); // Round down to page size.

					// Make sure this is free memory past the kernel.
					if(start < start_of_free_memory_at_boot)
						start = start_of_free_memory_at_boot;

					start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); // Round up to page size.


					#ifdef DEBUG
						if (end > start) {
							PrintString("Adding pages from ");
							PrintHex(start);
							PrintString(" to ");
							PrintHex(end - PAGE_SIZE);
							PrintChar('\n');
						}
					#endif

					// Now we will divide this memory up into pages and iterate through them.
					size_t page_addr;
					for(page_addr = start; page_addr < end; page_addr += PAGE_SIZE) {
						// Push this page onto the linked stack.

						// Map this physical memory, so can write the previous stack page to it.
						size_t *bp = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(page_addr);
						
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

// Indicates that we are done with the multiboot memory and that it can be released.
void DoneWithMultibootMemory() {
	// Frees the memory pages between the end of kernel memory
	size_t end_of_kernel_memory = (size_t)&bssEnd;
	size_t start = (end_of_kernel_memory + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); // Round up.
	size_t end = start_of_free_memory_at_boot;// (start_of_free_memory_at_boot + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); // Round up.
	
	#ifdef DEBUG
		PrintString("Done with multiboot memory, freeing from ");
		PrintHex(start);
		PrintString(" to ");
		PrintHex(end);
		PrintChar('\n');
	#endif

	for (size_t page = start; page < end; page += PAGE_SIZE) {
		#ifdef DEBUG
			PrintString("Unmapping ");
			PrintHex(page);
			PrintString(" at virtual address ");
			PrintHex(page + VIRTUAL_MEMORY_OFFSET);
			PrintChar('\n');
		#endif
		UnmapVirtualPage(kernel_pml4, page + VIRTUAL_MEMORY_OFFSET, true);
	}
}

// Grabs the next physical page (at boot time before the virtual memory allocator is initialized),
// returns OUT_OF_PHYSICAL_PAGES if there are no more physical pages.
size_t GetPhysicalPagePreVirtualMemory() {
	if(next_free_page_address == OUT_OF_PHYSICAL_PAGES) {
		// No more free pages.
		return OUT_OF_PHYSICAL_PAGES;
	}

	// Take the top page from the stack.
	size_t addr = next_free_page_address;

	// Pop it from the stack by mapping the page to physical memory so we can grab the pointer to the
	// next free page.
	size_t *bp = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(addr);
	next_free_page_address = *bp;

	free_pages--;

	return addr;
}

// Grabs the next physical page, returns OUT_OF_PHYSICAL_PAGES if there are no more physical pages.
size_t GetPhysicalPage() {
	if(next_free_page_address == OUT_OF_PHYSICAL_PAGES) {
		// Ran out of memory. Try to clean up some memory.
		CleanUpObjectPools();

		if (next_free_page_address == OUT_OF_PHYSICAL_PAGES) {
			// No more free pages.
			return OUT_OF_PHYSICAL_PAGES;
		}
	}

	// Take the top page from the stack.
	size_t addr = next_free_page_address;

	// Pop it from the stack by mapping the page to physical memory so we can grab the pointer to the
	// next free page.
	size_t *bp = (size_t *)TemporarilyMapPhysicalMemory(addr, 5);
	next_free_page_address = *bp;

	// Clear out the page, so we don't leak anything from another process.
	memset((unsigned char*)bp, 0, PAGE_SIZE);

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