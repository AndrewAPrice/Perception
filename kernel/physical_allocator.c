#include "physical_allocator.h"
#include "virtual_allocator.h"
#include "multiboot2.h"
#include "text_terminal.h"

size_t total_system_memory;
size_t free_pages;

const size_t page_size = 4096;

extern size_t _bssEnd; /* start of free memory on boot */

/* physical memory is divided into 4kb pages, we keep a linked stack of them that we can pop a page off of
   and push a page onto - this pointer points to the top of the stack (next free page) */
size_t page_frame_pointer;

void init_physical_allocator() {
	total_system_memory = 0;
	free_pages = 0;

	page_frame_pointer = 0; /* 0 is the same as null */

	/* search the multiboot header for the memory map */
	struct multiboot_tag *tag;
	for(tag = (struct multiboot_tag *)(size_t)(MultibootInfo.addr + 8);
		tag->type != MULTIBOOT_TAG_TYPE_END;
		tag = (struct multiboot_tag *)((size_t) tag + (size_t)((tag->size + 7) & ~7))) {
		
		if(tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
			/* found the memory map */
			struct multiboot_tag_mmap *mmap_tag = (struct multiboot_tag_mmap *)tag;

			/* print_string("Entry size: ");
			print_number(mmap_tag->entry_size);
			print_string(" Tag size: ");
			print_number(mmap_tag->size);
			print_string(" Entries: ");
			print_number(mmap_tag->size / mmap_tag->entry_size);
			print_string("\n"); */

			/*  iterate over each entry */
			struct multiboot_mmap_entry *mmap;
			for(mmap = mmap_tag->entries;
				(size_t)mmap < (size_t)tag + (size_t)tag->size;
				mmap = (struct multiboot_mmap_entry *)((size_t)mmap + (size_t)mmap_tag->entry_size)) {

				/* print_string("Base: ");
				print_hex(mmap->addr);
				print_string(" Length: ");
				print_number(mmap->len);
				print_string(" Type: ");
				print_number(mmap->type); 
				print_char('\n'); */

				if(mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
					/* this memory is avaliable for usage */
					total_system_memory += mmap->len;
					
					size_t start = mmap->addr;
					if(start < (size_t)&_bssEnd)
						start = (size_t)&_bssEnd; /* make sure this is free memory past the kernel */

					/* make the start and end aligned to page sizes */
					start = (start + page_size - 1) & ~(page_size - 1); /* round up */
					size_t end = (mmap->addr + mmap->len) & ~(page_size - 1); /* round down */

					size_t page_addr;
					for(page_addr = start; page_addr < end; page_addr += page_size) {
						/* add to the linked stack */
						size_t *bp = (size_t *)map_temp_boot_page(page_addr);
						/*print_hex(page_addr);
						print_string("->");
						print_hex((size_t)bp);
						print_string("\n");*/
						*bp = page_frame_pointer;
						page_frame_pointer = page_addr;

						/* add this free page to the list of free pages */
						free_pages++;
					}
				}

			}
		}
	}
}

/* grabs the next physical page (at boot time before the virtual memory allocator is initialized), returns 0 if there are no more physical pages */
size_t get_physical_page_boot() {
	if(page_frame_pointer == 0)
		return 0; /* no more free pages */

	/* take the top page from the stack */
	size_t addr = page_frame_pointer;

	/* update the pointer to the next free page */
	size_t *bp = (size_t *)map_temp_boot_page(addr);
	page_frame_pointer = *bp;

	free_pages--;

	return addr;
}

/* grabs the next physical page, returns 0 if there are no more physical pages  */
size_t get_physical_page() {
	if(page_frame_pointer == 0)
		return 0; /* no more free pages */

	/* take the top page from the stack */
	size_t addr = page_frame_pointer;

	/* update the pointer to the next free page */
	size_t *bp = (size_t *)map_physical_memory(addr, 0);
	page_frame_pointer = *bp;

	free_pages--;

	return addr;
}

/* frees a physical page */
void free_physical_page(size_t addr) {
	/* point to this page to the next stack entry */
	size_t *bp = (size_t *)map_physical_memory(addr, 0);
	*bp = page_frame_pointer;

	/* put this page on the top of the stack */
	page_frame_pointer = addr;

	free_pages++;
}

