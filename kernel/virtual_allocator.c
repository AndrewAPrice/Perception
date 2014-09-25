#include "virtual_allocator.h"
#include "physical_allocator.h"
#include "text_terminal.h"

/* our paging structures made at boot time, these can be freed after the virtual allocator has been initialized */
extern size_t Pml4[];
extern size_t Pdpt[];
extern size_t Pd[];


/* map a physical page so that we can access it - use this before the virtual allocator has been initialized */
void *map_temp_boot_page(size_t addr) {
	/* round this down to the nearest 2MB as we use 2MB pages before we setup the virtual allocator */
	size_t addr_start = addr & ~(2 * 1024 * 1024 - 1);

	size_t addr_offset = addr - addr_start;
	/*print_string("Requested: ");
	print_hex(addr);
	print_string("\nMapping ");
	print_hex(addr_start);
	print_string(" Offset: ");
	print_number(addr_offset);*/

	size_t entry = addr_start | 0x83;
	/* check if it different to what is currently loaded */
	if(Pd[511] != entry) {
		/* map this to the last page of our page directory we set up at boot time */
		Pd[511] = entry;

		/* flush our page table cache */
		__asm__ __volatile__("mov %0, %%cr3":: "a"(Pml4));
	}

	/* the virtual address of the temp page - 1GB - 2MB */
	size_t temp_page_boot = 1022 * 1024 * 1024;

	/*print_string(" vir: ");
	print_hex(temp_page_boot + addr_offset);
	print_string("\nPd[511]:");
	print_hex(Pd[511]);
	print_char('\n');*/

	return (void *)(temp_page_boot + addr_offset);
}

size_t kernel_pml4; /* address of the new kernel pml4 */
size_t *tempMemoryPageTable; /* pointer to a page table for our temp memory */
size_t tempMemoryStart; /* start address of what the temp page table refers to */

size_t current_pml4; /* the currently loaded pml4 */


extern size_t _bssEnd; /* start of free memory on boot */

/* maps a physical address to a virtual address in the kernel - at boot time while paging is initializing,
  assignPageTable - true if we're assigning a page table (for our temp memory) rather than a page */
void map_kernel_mem_boot(size_t virtualaddr, size_t physicaladdr, bool assignPageTable) {
	/* find index into each pml table */
	/* 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	   4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
                           #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
                           pml4       pml3       pml2       pml1        flags */
	size_t pml4Entry = (virtualaddr >> 39) & 511;
	size_t pml3Entry = (virtualaddr >> 30) & 511;
	size_t pml2Entry = (virtualaddr >> 21) & 511;
	size_t pml1Entry = (virtualaddr >> 12) & 511;

	/* look in pml4 */
	size_t *ptr = (size_t *)map_temp_boot_page(kernel_pml4);
	if(ptr[pml4Entry] == 0) {
		/* entry blank, create a pml3 table */
		size_t new_pml3 = get_physical_page_boot();

		/* clear it */
		ptr = (size_t *)map_temp_boot_page(new_pml3);
		size_t i;
		for(i = 0; i < 512; i++)
			ptr[i] = 0;

		/* switch back to the pml4 */
		ptr = (size_t *)map_temp_boot_page(kernel_pml4);

		/* write it in */
		ptr[pml4Entry] = new_pml3 | 0x1;

	}

	size_t pml3 = ptr[pml4Entry] & ~(page_size - 1);

	/* look in pml3 */
	ptr = (size_t *)map_temp_boot_page(pml3);
	if(ptr[pml3Entry] == 0) {
		/* entry blank, create a pml2 table */
		size_t new_pml2 = get_physical_page_boot();

		/* clear it */
		ptr = (size_t *)map_temp_boot_page(new_pml2);
		size_t i;
		for(i = 0; i < 512; i++)
			ptr[i] = 0;

		/* switch back to the pml3 */
		ptr = (size_t *)map_temp_boot_page(pml3);

		/* write it in */
		ptr[pml3Entry] = new_pml2 | 0x1;
	}

	size_t pml2 = ptr[pml3Entry] & ~(page_size - 1);

	if(assignPageTable) {
		/* we're assigning a page table to the pml2 rather than a page to the pml1 */
		ptr = (size_t *)map_temp_boot_page(pml2);
		size_t entry = physicaladdr | 0x1;
		ptr[pml2Entry] = entry;
		return;
	}

	/* look in pml2 */
	ptr = (size_t *)map_temp_boot_page(pml2);
	if(ptr[pml2Entry] == 0) {
		/* entry blank, create a pml1 table */
		size_t new_pml1 = get_physical_page_boot();

		/* clear it */
		ptr = (size_t *)map_temp_boot_page(new_pml1);
		size_t i;
		for(i = 0; i < 512; i++)
			ptr[i] = 0;

		/* switch back to the pml2 */
		ptr = (size_t *)map_temp_boot_page(pml2);

		/* write it in */
		ptr[pml2Entry] = new_pml1 | 0x1;
	}

	size_t pml1 = ptr[pml2Entry] & ~(page_size - 1);

	/* write us in pml1 */
	ptr = (size_t *)map_temp_boot_page(pml1);
	size_t entry = physicaladdr | 0x3;
	ptr[pml1Entry] = entry;
}

/* Initialies the virtual allocator */
void init_virtual_allocator() {
	/* we entered long mode with a temporary setup, now it's time to build a real paging system for us */
	kernel_pml4 = get_physical_page_boot();
	
	/* clear the pml4 */
	size_t *ptr = (size_t *)map_temp_boot_page(kernel_pml4);
	size_t i;
	for(i = 0; i < 512; i++)
		ptr[i] = 0;

	/* figure out the top of memory, past the loaded code */
	size_t topOfMem = ((size_t)&_bssEnd + page_size - 1) & ~(page_size - 1); /* round up */

	/* map the booted code into memory */
	for(i = 0; i < topOfMem; i += page_size)
		map_kernel_mem_boot(i + virtual_memory_offset, i, false);
	i += virtual_memory_offset;

	print_string("Top of memory: ");
	print_hex(topOfMem);
	print_char('\n');

	/* allocate our page table for our temp stuff */
	tempMemoryPageTable = (size_t *)i;
	i += page_size;

	size_t physical_tempMemoryPageTable = get_physical_page_boot();
	map_kernel_mem_boot((size_t)tempMemoryPageTable, physical_tempMemoryPageTable, false);

	/* maps the next 2MB range in memory*/
	size_t page_table_range = page_size * 512;
	tempMemoryStart = (i + page_table_range) & ~(page_table_range - 1);

	print_string("Temp memory range: ");
	print_hex(tempMemoryStart);
	print_char('\n');
	map_kernel_mem_boot(tempMemoryStart, physical_tempMemoryPageTable, true);


	/* clear out our temp page table so we don't think it's free to allocate stuff into */
	ptr = (size_t *)map_temp_boot_page(physical_tempMemoryPageTable);
	for(i = 0; i < 512; i++)
		ptr[i] = 1; /* assigned */

	/* Flush and load the new page directory */
	current_pml4 = 1; /* non-page aligned, a dud entry so switch_to_address_space works */
	switch_to_address_space(kernel_pml4);

	/* reclaim the Pml4, Pdpt, Pd set up at boot time */
	free_physical_page((size_t)Pml4);
	free_physical_page((size_t)Pdpt);
	free_physical_page((size_t)Pd);
	return;
}



/* maps a physical page (page aligned) into virtual memory so we can fiddle with it,
  index is from 0 to 511 - mapping a different address to the same index
  unmaps the previous page */
void *map_physical_memory(size_t addr, size_t index) {
	size_t entry = addr | 0x3;
	
	/* check if it's not already mapped */
	if(tempMemoryPageTable[index] != entry) {
		/* map this page into our temporary page table */
		tempMemoryPageTable[index] = entry;

		/* flush our page table cache */
		__asm__ __volatile__("mov %0, %%cr3":: "a"(current_pml4));
	}

	return (void *)(tempMemoryStart + page_size * index);
}


#if 0
/* every we need to keep a table of physical and virtual address"es for the subpages */
size_t kernel_pml4;
size_t kernel_pml3;

/* current loaded physical pml4 */
size_t current_pml4;

/* KERNEL MEMORY */
/* Initialies the virtual allocator, assumes the long mode was entered with
   the pml3 identity mapped to the top and 1st 256gb with 1gb pages. Once initialised
   the virtual memory manages switches to 4kb pages. */


/* Creates a process's virtual address space. */
bool create_process_address_space(size_t *pml4) {
	*pml4 = find_physical_page();
	if(pml4 == 0) return false; /* no memory */

	size_t *virtual_pml4 = (size_t *)(*pml4 + virtual_memory_offset);
	/* clear out the first entries */
	size_t i; for(i = 0; i < 511; i++)
		virtual_pml4[i] = 0;

	/* copy to pointer to the kernel's pml3 into here */
	virtual_pml4[511] = ((size_t *)(kernel_pml4 + virtual_memory_offset))[511];
	return true;
}

/* Frees a process's virtual address space. */
void free_process_address_space(size_t pml4) {
	/* switch to kernel memory */
	switch_to_address_space(kernel_pml4);

	size_t *virtual_pml4 = (size_t *)(pml4 + virtual_memory_offset);
	size_t i; for(i = 0; i < 256; i++) {
		/* scan lower half of virtual memory */
		if(virtual_pml4[i] != 0) {
			/* there's an entry */
			size_t pml3 = virtual_pml4[i] & ~4096;
			size_t *virtual_pml3 = (size_t*)(pml3 + virtual_memory_offset);
			size_t j; for(j = 0; j < 512; j++) {
				if(virtual_pml3[j] != 0) {
					/* there's an entry */
					size_t pml2 = virtual_pml3[j] & ~4096;
					size_t *virtual_pml2 = (size_t*)(pml2 + virtual_memory_offset);

					size_t k; for(k = 0; k < 512; k++) {
						if(virtual_pml2[k] != 0) {
							/* there's an entry */
							size_t pml1 = virtual_pml2[k] & ~4096;
							size_t *virtual_pml1 = (size_t*)(pml1 + virtual_memory_offset);

							size_t l; for(l = 0; l < 512; l++) {
								if(virtual_pml1[l] != 0) {
									/* there's an entry */
									size_t page = virtual_pml1[l] & ~4096;
									free_physical_page(page);
								}
							}
							free_physical_page(pml1);
						}
					}
					free_physical_page(pml2);
				}
			}

			free_physical_page(pml3);
		}
	}

	/* free this pml4 */
	free_physical_page(pml4);
}

/* Finds a range of free process virtual pages. Returns 0 if it can't fit it.  Doesn't allocate it. */
size_t find_free_process_page_range(size_t pml4, size_t pages) {
	if(pages == 0 || pages > 34359738368)
		return 0; /* too many or not enough */

	size_t start_pml1_entry = 0;
	size_t start_pml2_entry = 0;
	size_t start_pml3_entry = 0;
	size_t start_pml4_entry = 0;

	bool counting = false;
	size_t pages_counted = 0;


	size_t *virtual_pml4 = (size_t *)(pml4 + virtual_memory_offset);
	/* scan pml4 */
	size_t i; for(i = 0; i < 256 && pages_counted < pages; i++) {
		if(virtual_pml4[i] == 0) {
			if(!counting) {
				counting = true;
				start_pml1_entry = 0;
				start_pml2_entry = 0;
				start_pml3_entry = 0;
				start_pml4_entry = i;
			}
			pages += 512 * 512 * 512;
		} else {
			/* there's an entry */
			size_t pml3 = virtual_pml4[i] & ~4096;
			size_t *virtual_pml3 = (size_t*)(pml3 + virtual_memory_offset);
			/* scan pml3 */
			size_t j; for(j = 0; j < 512 && pages_counted < pages; j++) {
				if(virtual_pml3[j] == 0) {
					if(!counting) {
						counting = true;
						start_pml1_entry = 0;
						start_pml2_entry = 0;
						start_pml3_entry = j;
						start_pml4_entry = i;
					}
					pages += 512 * 512;
				} else {
					/* there's an entry */
					size_t pml2 = virtual_pml3[j] & ~4096;
					size_t *virtual_pml2 = (size_t*)(pml2 + virtual_memory_offset);
					/* scan pml2 */
					size_t k; for(k = 0; k < 512 && pages_counted < pages; k++) {
						if(virtual_pml2[k] == 0) {
							if(!counting) {
								counting = true;
								start_pml1_entry = 0;
								start_pml2_entry = k;
								start_pml3_entry = j;
								start_pml4_entry = i;
							}
							pages += 512;
						} else {
							/* there's an entry */
							size_t pml1 = virtual_pml2[k] & ~4096;
							size_t *virtual_pml1 = (size_t*)(pml1 + virtual_memory_offset);
							/* scan pml1 */
							size_t l; for(l = 0; l < 512 && pages_counted < pages; l++) {
								if(virtual_pml1[l] == 0) {
									if(!counting) {
										counting = true;
										start_pml1_entry = l;
										start_pml2_entry = k;
										start_pml3_entry = j;
										start_pml4_entry = i;
									}
									pages++;
								} else {
									/* there's an entry */
									counting = false;
									pages = 0;
								}
							}
						}
					}
				}
			}
		}
	}

	if(!counting || pages_counted < pages)
		return 0; /* couldn't find anywhere large enough */

	/* convert each pml table index into an address */
	/* 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	   4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
                           #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
                           pml4       pml3       pml2       pml1        flags */
	size_t addr = (start_pml4_entry << 39) | (start_pml3_entry << 30) | (start_pml2_entry << 21) | (start_pml1_entry << 12);
	return addr;
}

/* Frees a process virtual page. */
void free_process_page(size_t pml4, size_t addr) {
	if(addr >= 0x8000000000000000)
		return; /* in kernel space */

	/* find index into each pml table */
	/* 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	   4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
                           #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
                           pml4       pml3       pml2       pml1        flags */
	size_t pml4Entry = (addr >> 39) & 511;
	size_t pml3Entry = (addr >> 30) & 511;
	size_t pml2Entry = (addr >> 21) & 511;
	size_t pml1Entry = (addr >> 12) & 511;

	/* make sure we have an entry in each pml table */
	size_t *virtual_pml4 = (size_t *)(pml4 + virtual_memory_offset);
	if(virtual_pml4[pml4Entry] == 0)
		return;

	size_t pml3 = virtual_pml4[pml4Entry] & ~4096;
	size_t *virtual_pml3 = (size_t *)(pml3 + virtual_memory_offset);
	if(virtual_pml3[pml3Entry] == 0)
		return;

	size_t pml2 = virtual_pml3[pml3Entry] & ~4096;
	size_t *virtual_pml2 = (size_t *)(pml2 + virtual_memory_offset);
	if(virtual_pml2[pml2Entry] == 0)
		return;

	size_t pml1 = virtual_pml2[pml2Entry] & ~4096;
	size_t *virtual_pml1 = (size_t *)(pml1 + virtual_memory_offset);
	if(virtual_pml1[pml1Entry] == 0)
		return;

	/* found it! */
	size_t phys_addr = virtual_pml1[pml1Entry] & ~4096;
	free_physical_page(phys_addr); /* free this page */

	/* remove this entry, and then scan each pml level to see if we can free that too */
	virtual_pml1[pml1Entry] = 0;
	bool found = false;
	size_t i; for(i = 0; i < 512; i++) {
		if(virtual_pml1[pml1Entry] != 0) {
			found = true;
			break;
		}
	}

	if(!found) {
		/* erase this pml1 from pml2 */
		free_physical_page(pml1);
		virtual_pml2[pml2Entry] = 0;
		for(i = 0; i < 512; i++) {
			if(virtual_pml2[pml2Entry] != 0) {
				found = true;
				break;
			}
		}

		if(!found) {
			/* erase this pml2 from pml3 */
			free_physical_page(pml2);
			virtual_pml3[pml3Entry] = 0;
			for(i = 0; i < 512; i++) {
				if(virtual_pml3[pml3Entry] != 0) {
					found = true;
					break;
				}
			}

			if(!found) {
				/* erase this pml3 from pml4 */
				free_physical_page(pml3);
				virtual_pml4[pml4Entry] = 0;

				/* don't scan pml4, because that is only destroyed when the process exits */
			}
		}
	}

	/* flush the lookup buffer for this address */
	if(current_pml4 == pml4) /* is this check neccessary? would this ever be called outside of a process's memory space? */
		flush_virtual_page(addr);
}

/* Assignes a process page to a physical page. */
bool assign_process_page_to_physical_page(size_t pml4, size_t physical_page, size_t virtual_page) {
	physical_page &= ~4096;
	if(virtual_page >= 0x8000000000000000)
		return false; /* in kernel space */

	size_t pml4_flags = 0;
	size_t pml3_flags = 0;
	size_t pml2_flags = 0;
	size_t pml1_flags = 0;

	/* find index into each pml table */
	/* 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	   4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
                           #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
                           pml4       pml3       pml2       pml1        flags */
	size_t pml4Entry = (virtual_page >> 39) & 511;
	size_t pml3Entry = (virtual_page >> 30) & 511;
	size_t pml2Entry = (virtual_page >> 21) & 511;
	size_t pml1Entry = (virtual_page >> 12) & 511;

	bool allocatedPml3, allocatedPml2;
	size_t pml3, pml2, pml1;

	/* make sure we have an entry in each pml table */
	size_t *virtual_pml4 = (size_t *)(pml4 + virtual_memory_offset);
	if(virtual_pml4[pml4Entry] == 0) {
		/* assign a page */
		pml3 = find_physical_page();
		if(pml3 == 0)
			return false; /* out of memory */
		mark_physical_page(pml3);
		allocatedPml3 = true;
		virtual_pml4[pml4Entry] = pml3 | pml4_flags;
	}
	else {
		allocatedPml3 = false;
		pml3 = virtual_pml4[pml4Entry] & ~4096;
	}

	size_t *virtual_pml3 = (size_t *)(pml3 + virtual_memory_offset);
	if(virtual_pml3[pml3Entry] == 0) {
		/* assign a page */
		pml2 = find_physical_page();
		if(pml2 == 0) {
			if(allocatedPml3) {
				free_physical_page(pml3);
				virtual_pml4[pml4Entry] = 0;
			}
			return false; /* out of memory */
		}
		mark_physical_page(pml2);
		allocatedPml2 = true;
		virtual_pml3[pml3Entry] = pml2 | pml3_flags;
	} else {
		allocatedPml2 = false;
		pml2 = virtual_pml3[pml3Entry] & ~4096;
	}

	size_t *virtual_pml2 = (size_t *)(pml2 + virtual_memory_offset);
	if(virtual_pml2[pml2Entry] == 0) {
		/* assign a page */
		pml1 = find_physical_page();
		if(pml1 == 0) {
			if(allocatedPml2) {
				free_physical_page(pml2);
				virtual_pml3[pml3Entry] = 0;
			}

			if(allocatedPml3) {
				free_physical_page(pml3);
				virtual_pml4[pml4Entry] = 0;
			}
			return false; /* out of memory */
		}
		mark_physical_page(pml1);
		virtual_pml2[pml2Entry] = pml1 | pml2_flags;
	} else {
		pml1 = virtual_pml2[pml2Entry] & ~4096;
	}

	size_t *virtual_pml1 = (size_t *)(pml1 + virtual_memory_offset);
	if(virtual_pml1[pml1Entry] == 0) {
		/* not allocated - we can assign this page! */
		virtual_pml1[pml1Entry] = physical_page | pml1_flags;

		if(current_pml4 == pml4) /* is this check neccessary? would this ever be called outside of a process's memory space? */
			flush_virtual_page(physical_page);

		return true;
	} else
		return false; /* page already allocated */
}
#endif
/* Switch to a virtual address space. */
void switch_to_address_space(size_t pml4) {
	if(pml4 != current_pml4) {
		current_pml4 = pml4;
		__asm__ __volatile__("mov %0, %%cr3":: "b"(pml4));
	}
}

/* Flush the CPU lookup for a particular virtual address */
void flush_virtual_page(size_t addr) {
	switch_to_address_space(current_pml4);

	/*
	This gives me an assembler error:
		Error: junk `(%rbp))' after expression
	 __asm__ __volatile__ ( "invlpg (%0)" : : "m"(addr) : "memory" );
	 */
}
