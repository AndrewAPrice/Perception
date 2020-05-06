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
		flush_virtual_page(addr_start);
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

void map_kernel_mem_boot(size_t virtualaddr, size_t physicaladdr, bool assignPageTable);

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


	/* allocate our page table for our temp stuff */
	tempMemoryPageTable = (size_t *)i;
	i += page_size;

	size_t physical_tempMemoryPageTable = get_physical_page_boot();
	map_kernel_mem_boot((size_t)tempMemoryPageTable, physical_tempMemoryPageTable, false);

	/* maps the next 2MB range in memory*/
	size_t page_table_range = page_size * 512;
	tempMemoryStart = (i + page_table_range) & ~(page_table_range - 1);

	map_kernel_mem_boot(tempMemoryStart, physical_tempMemoryPageTable, true);


	/* clear out our temp page table so we don't think it's free to allocate stuff into */
	ptr = (size_t *)map_temp_boot_page(physical_tempMemoryPageTable);
	for(i = 0; i < 512; i++)
		ptr[i] = 1; /* assigned */

	/* Flush and load the new page directory */
	current_pml4 = 1; /* non-page aligned, a dud entry so switch_to_address_space works */
	switch_to_address_space(kernel_pml4);

	/* reclaim the Pml4, Pdpt, Pd set up at boot time */
	unmap_physical_page(kernel_pml4, (size_t)Pml4 + virtual_memory_offset, true);
	unmap_physical_page(kernel_pml4, (size_t)Pdpt + virtual_memory_offset, true);
	unmap_physical_page(kernel_pml4, (size_t)Pd + virtual_memory_offset, true);
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

/* find a range of free physical pages in kernel memory - returns the first addres or 0 if it can't find a fit */
size_t find_free_page_range(size_t pml4, size_t pages) {
	if(pages == 0 || pages > 34359738368)
		return 0; /* too many or not enough */
	/* store the first entry when we scan */

	size_t start_pml1_entry = 0;
	size_t start_pml2_entry = 0;
	size_t start_pml3_entry = 0;
	size_t start_pml4_entry = 0;

	bool counting = false; /* have we found an area and started counting? */
	size_t pages_counted = 0; /* how many pages we have counted so far? terminates when pages == pages_counted */

	size_t pml4_scan_start, pml4_scan_end;
	if(pml4 == kernel_pml4) {
		/* kernel space, scan higher half */
		pml4_scan_start = 256;
		pml4_scan_end = 512;
	} else {
		/* user space, scan lower half */
		pml4_scan_start = 0;
		pml4_scan_end = 256;
	}


	/* scan pml4 */
	size_t *ptr = (size_t *)map_physical_memory(pml4, 0);
	size_t i;
	for(i = pml4_scan_start; i < pml4_scan_end && pages_counted < pages; i++) {
		if(ptr[i] == 0) {
			if(!counting) {
				counting = true;
				start_pml1_entry = 0;
				start_pml2_entry = 0;
				start_pml3_entry = 0;
				start_pml4_entry = i;
			}
			pages_counted += 512 * 512 * 512;
		} else {
			/* there's an entry */
			size_t pml3 = ptr[i] & ~(page_size - 1);

			/* scan pml3 */
			ptr = (size_t *)map_physical_memory(pml3, 1);
			size_t j;
			for(j = 0; j < 512 && pages_counted < pages; j++) {
				if(ptr[j] == 0) {
					if(!counting) {
						counting = true;
						start_pml1_entry = 0;
						start_pml2_entry = 0;
						start_pml3_entry = j;
						start_pml4_entry = i;
					}
					pages_counted += 512 * 512;
				} else {
					/* there's an entry */
					size_t pml2 = ptr[j] & ~(page_size - 1);
					
					/* scan pml2 */
					ptr = (size_t *)map_physical_memory(pml2, 2);
					size_t k;
					for(k = 0; k < 512 && pages_counted < pages; k++) {
						if(ptr[k] == 0) {
							if(!counting) {
								counting = true;
								start_pml1_entry = 0;
								start_pml2_entry = k;
								start_pml3_entry = j;
								start_pml4_entry = i;
							}
							pages_counted += 512;
						} else {
							/* there's an entry */
							size_t pml1 = ptr[k] & ~(page_size - 1);

							/* scan pml1 */
							ptr = (size_t *)map_physical_memory(pml1, 3);
							size_t l;
							for(l = 0; l < 512 && pages_counted < pages; l++) {
								if(ptr[l] == 0) {
									if(!counting) {
										counting = true;
										start_pml1_entry = l;
										start_pml2_entry = k;
										start_pml3_entry = j;
										start_pml4_entry = i;
									}
									pages_counted++;
								} else {
									/* there's an entry */
									counting = false;
									pages_counted = 0;
								}
							}

							/* return the pointer to pml2 */
							ptr = (size_t *)map_physical_memory(pml2, 2);
						}
					}

					/* return the pointer to pml3 */
					ptr = (size_t *)map_physical_memory(pml3, 1);
				}
			}

			/* return the pointer to pml4 */
			ptr = (size_t *)map_physical_memory(pml4, 0);
		}
	}

	if(!counting || pages_counted < pages) {
		print_string("Failed: Counting: ");
		print_number(counting);
		print_string(" Pages Counted: ");
		print_number(pages_counted);
		print_string(" Pages: ");
		print_number(pages);
		return 0; /* couldn't find anywhere large enough */
	}


	/* convert each pml table index into an address */
	/* 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	   4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
                           #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
                           pml4       pml3       pml2       pml1        flags */
	size_t addr = (start_pml4_entry << 39) | (start_pml3_entry << 30) | (start_pml2_entry << 21) | (start_pml1_entry << 12);
	
	/* make the address canonical in kernel space */
	if(start_pml4_entry >= 256)
		addr += 0xFFFF000000000000;
	
	return addr;
}

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

/* maps a physical page to a virtual page */
bool map_physical_page(size_t pml4, size_t virtualaddr, size_t physicaladdr) {
	/* find index into each pml table */
	/* 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	   4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
                           #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
                           pml4       pml3       pml2       pml1        flags */
	size_t pml4Entry = (virtualaddr >> 39) & 511;
	size_t pml3Entry = (virtualaddr >> 30) & 511;
	size_t pml2Entry = (virtualaddr >> 21) & 511;
	size_t pml1Entry = (virtualaddr >> 12) & 511;

	if(pml4 == kernel_pml4) {
		/* kernel must be higher half */
		if(pml4Entry < 256)
			return false;
	} else {
		/* user space must be lower half */
		if(pml4Entry >= 256)
			return false;
	}

	bool createdPml3, createdPml2;

	/* look in pml4 */
	size_t *ptr = (size_t *)map_physical_memory(pml4, 0);
	if(ptr[pml4Entry] == 0) {
		/* entry blank, create a pml3 table */
		size_t new_pml3 = get_physical_page();
		if(new_pml3 == 0)
			return false; /* no space for pml3 */

		/* clear it */
		ptr = (size_t *)map_physical_memory(new_pml3, 1);
		size_t i;
		for(i = 0; i < 512; i++)
			ptr[i] = 0;

		/* switch back to the pml4 */
		ptr = (size_t *)map_physical_memory(pml4, 0);

		/* write it in */
		ptr[pml4Entry] = new_pml3 | 0x1;

		createdPml3 = true;
	} else
		createdPml3 = false;

	size_t pml3 = ptr[pml4Entry] & ~(page_size - 1);

	/* look in pml3 */
	ptr = (size_t *)map_physical_memory(pml3, 1);
	if(ptr[pml3Entry] == 0) {
		/* entry blank, create a pml2 table */
		size_t new_pml2 = get_physical_page();
		if(new_pml2 == 0) {
			/* no space for pml2 */
			if(createdPml3) {
				ptr = (size_t *)map_physical_memory(pml4, 0);
				ptr[pml4Entry] = 0;
				free_physical_page((size_t)pml3);
			}
			return false;
		}

		/* clear it */
		ptr = (size_t *)map_physical_memory(new_pml2, 2);
		size_t i;
		for(i = 0; i < 512; i++)
			ptr[i] = 0;

		/* switch back to the pml3 */
		ptr = (size_t *)map_physical_memory(pml3, 1);

		/* write it in */
		ptr[pml3Entry] = new_pml2 | 0x1;

		createdPml2 = true;
	} else
		createdPml2 = false;

	size_t pml2 = ptr[pml3Entry] & ~(page_size - 1);

	/* look in pml2 */
	ptr = (size_t *)map_physical_memory(pml2, 2);
	if(ptr[pml2Entry] == 0) {
		/* entry blank, create a pml1 table */
		size_t new_pml1 = get_physical_page();
		if(new_pml1 == 0) {
			/* no space for pml1 */
			if(createdPml2) {
				ptr = (size_t *)map_physical_memory(pml3, 1);
				ptr[pml3Entry] = ((size_t)pml3);
			}
			
			if(createdPml3) {
				ptr = (size_t *)map_physical_memory(pml4, 0);
				ptr[pml4Entry] = 0;
				free_physical_page((size_t)pml3);
			}
			return false;
		}

		/* clear it */
		ptr = (size_t *)map_physical_memory(new_pml1, 3);
		size_t i;
		for(i = 0; i < 512; i++)
			ptr[i] = 0;

		/* switch back to the pml2 */
		ptr = (size_t *)map_physical_memory(pml2, 2);

		/* write it in */
		ptr[pml2Entry] = new_pml1 | 0x1;
	}

	size_t pml1 = ptr[pml2Entry] & ~(page_size - 1);

	/* check if this address has already been mapped in pml1 */
	ptr = (size_t *)map_physical_memory(pml1, 3);;
	if(ptr[pml1Entry] != 0)
		return false; /* don't worry about cleaning up because for it to be mapped pml2 and pml3 must already exist */

	/* write us in pml1 */
	size_t entry = physicaladdr | 0x3;
	ptr[pml1Entry] = entry;


	if(createdPml3 && pml4Entry >= 256) {
		/* we modified the kernel's pml4, copy this into every user application's pml4 */
	}

	if(pml4 == current_pml4 || pml4Entry >= 256) {
		/* we are in this address space or we're part of the kernel, flush the tlb */
		flush_virtual_page(virtualaddr);
	}

	return true;
}

/* unmaps a physical page - free specifies if that page should be returned to the physical memory manager */
void unmap_physical_page(size_t pml4, size_t virtualaddr, bool free) {
	/* find index into each pml table */
	/* 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	   4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
                           #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
                           pml4       pml3       pml2       pml1        flags */

	size_t pml4Entry = (virtualaddr >> 39) & 511;
	size_t pml3Entry = (virtualaddr >> 30) & 511;
	size_t pml2Entry = (virtualaddr >> 21) & 511;
	size_t pml1Entry = (virtualaddr >> 12) & 511;

	if(pml4 == kernel_pml4) {
		/* kernel must be higher half */
		if(pml4Entry < 256)
			return;
	} else {
		/* user space must be lower half */
		if(pml4Entry >= 256)
			return;
	}

	/* look in pml4 */
	size_t *ptr = (size_t *)map_physical_memory(pml4, 0);
	if(ptr[pml4Entry] == 0)
		return;

	size_t pml3 = ptr[pml4Entry] & ~(page_size - 1);

	/* look in pml3 */
	ptr = (size_t *)map_physical_memory(pml3, 1);
	if(ptr[pml3Entry] == 0)
		return;

	size_t pml2 = ptr[pml3Entry] & ~(page_size - 1);

	/* look in pml2 */
	ptr = (size_t *)map_physical_memory(pml2, 2);
	if(ptr[pml2Entry] == 0)
		return;

	size_t pml1 = ptr[pml2Entry] & ~(page_size - 1);

	/* look in pml1 */
	ptr = (size_t *)map_physical_memory(pml1, 3);
	if(ptr[pml1Entry] == 0)
		return;

	/* we're here, so this was actually mapped somewhere */
	if(free) {
		/* return this memory to the physical allocator - you don't want to do this if it's shared or memory-mapped io */
		size_t physicaladdr = ptr[pml1Entry] & ~(page_size - 1);
		free_physical_page(physicaladdr);

		/* load this pointer again icnase free_physical_page maps something else */
		ptr = (size_t *)map_physical_memory(pml1, 3);
	}

	/* remote this entry */
	ptr[pml1Entry] = 0;

	/* scan to see if we can clear these pml tables*/
	bool found = 0;
	size_t i;
	/* scan pml1 */
	for(i = 0; i < 512; i++) {
		if(ptr[i] != 0) {
			found = 1;
			break;
		}
	}

	if(!found) {
		/* free the pml1 */
		free_physical_page(pml1);

		/* remove from pml2 */
		ptr = (size_t *)map_physical_memory(pml2, 2);
		ptr[pml2Entry] = 0;

		/* scan pml2 */
		for(i = 0; i < 512; i++) {
			if(ptr[i] != 0) {
				found = 1;
				break;
			}
		}

		if(!found) {
			/* free the pml2 */
			free_physical_page(pml2);

			/* remove from pml3 */
			ptr = (size_t *)map_physical_memory(pml3, 1);
			ptr[pml3Entry] = 0;

			/* scan pml3 */
			for(i = 0; i < 512; i++) {
				if(ptr[i] != 0) {
					found = 1;
					break;
				}
			}

			if(!found) {
				/* free the pml3 */
				free_physical_page(pml3);

				/* remove from pml4 */
				ptr = (size_t *)map_physical_memory(pml4, 0);
				ptr[pml4Entry] = 0;

				if(pml4Entry >= 256) {
					/* we modified the kernel's pml4, copy this into every user application's pml4 */
				}
			}
		}

	}

	if(pml4 == current_pml4 || pml4Entry >= 256) {
		/* we are in this address space or we're part of the kernel, flush the tlb */
		flush_virtual_page(virtualaddr);
	}
}


/* Creates a process's virtual address space, returns the pml4 - pml4 is 0 if it fails. */
size_t create_process_address_space() {
	size_t pml4 = get_physical_page();
	if(pml4 == 0) return 0; /* no memory */

	/* clear out the bottom half of this virtual address space */
	size_t *ptr = (size_t *)map_physical_memory(pml4, 0);
	size_t i;
	for(i = 0; i < 256; i++)
		ptr[i] = 0;

	/* copy the top half of the kernel's address space into this */
	size_t *kernel_ptr = (size_t *)map_physical_memory(kernel_pml4, 1);
	for(i = 256; i < 512; i++)
		ptr[i] = kernel_ptr[i];
	
	return pml4;
}


/* Frees a process's virtual address space. Everything it finds will be returned to the physical allocator so unmap any shared memory before. */
void free_process_address_space(size_t pml4) {
	/* if we're working in this address space, switch to kernel space*/
	if(current_pml4 == pml4)
		switch_to_address_space(kernel_pml4);

	/* scan the lower half of pml4 */
	size_t *ptr = (size_t *)map_physical_memory(pml4, 0);
	size_t i;
	for(i = 0; i < 256; i++) {
		if(ptr[i] != 0) {
			/* found a pml3 */
			size_t pml3 = ptr[i] & ~(page_size - 1);

			/* scan the pml3 */
			ptr = (size_t *)map_physical_memory(pml3, 1);
			size_t j;
			for(j = 0; j < 512; j++) {
				if(ptr[j] != 0) {
					/* found a pml2 */
					size_t pml2 = ptr[j] & ~(page_size - 1);

					/* scan the pml2 */
					ptr = (size_t *)map_physical_memory(pml2, 2);
					size_t k;
					for(k = 0; k < 512; k++) {
						if(ptr[k] != 0) {
							/* found a pml1 */
							size_t pml1 = ptr[k] & ~(page_size - 1);

							/* scan the pml1 */
							ptr = (size_t *)map_physical_memory(pml1, 3);
							size_t l;
							for(l = 0; l < 512; l++) {
								if(ptr[l] != 0) {
									/* found a page */
									size_t physicaladdr = ptr[l] & ~(page_size - 1);

									/* free the page */
									free_physical_page(physicaladdr);

									/* point back to pml1 */
									ptr = (size_t *)map_physical_memory(pml1, 3);
								}
							}

							/* free the pml1 */
							free_physical_page(pml1);

							/* point back to pml2 */
							ptr = (size_t *)map_physical_memory(pml2, 2);

						}
					}

					/* free the pml2 */
					free_physical_page(pml2);

					/* point back to pml3 */
					ptr = (size_t *)map_physical_memory(pml3, 1);
				}
			}

			/* free the pml3 */
			free_physical_page(pml3);

			/* point back to pml4 */
			ptr = (size_t *)map_physical_memory(pml4, 0);
		}

	}

	/* free the pml4 */
	free_physical_page(pml4);
}

/* Switch to a virtual address space. */
void switch_to_address_space(size_t pml4) {
	if(pml4 != current_pml4) {
		current_pml4 = pml4;
		__asm__ __volatile__("mov %0, %%cr3":: "b"(pml4));
	}
}

/* Flush the CPU lookup for a particular virtual address */
void flush_virtual_page(size_t addr) {
	//*switch_to_address_space(current_pml4);*/

	/*
	This gives me an assembler error:
		Error: junk `(%rbp))' after expression*/
	 __asm__ __volatile__ ( "invlpg (%0)" : : "b"(addr) : "memory" );
}
