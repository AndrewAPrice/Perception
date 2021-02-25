#include "virtual_allocator.h"

#include "object_pools.h"
#include "physical_allocator.h"
#include "process.h"
#include "shared_memory.h"
#include "text_terminal.h"

// Our paging structures made at boot time, these can be freed after the virtual allocator has been initialized.
extern size_t Pml4[];
extern size_t Pdpt[];
extern size_t Pd[];

// The address of the kernel's PML4.
size_t kernel_pml4;
// Pointer to a page table that we use when we want to temporarily map physical memory.
size_t *temp_memory_page_table;
// Start address of what the temporary page table refers to.
size_t temp_memory_start;

// The address of the currently loaded PML4.
size_t current_pml4;

// Start of the free memory on boot.
extern size_t bssEnd;

// The size of the page table, in bytes.
#define PAGE_TABLE_SIZE 4096 // 4 KB

// The size of a page table entry, in bytes.
#define PAGE_TABLE_ENTRY_SIZE 8

// The number of entries in a page table. Each entry is 8 bytes long.
#define PAGE_TABLE_ENTRIES (PAGE_TABLE_SIZE / PAGE_TABLE_ENTRY_SIZE)


// Maps a physical address to a virtual address in the kernel - at boot time while paging is initializing.
// assign_page_table - true if we're assigning a page table (for our temp memory) rather than a page.
void MapKernelMemoryPreVirtualMemory(size_t virtualaddr, size_t physicaladdr, bool assign_page_table) {
	// Find the index into each PML table:
	// 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	// 4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
    //                     #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
    //                     pml4       pml3       pml2       pml1        flags
	size_t pml4_entry = (virtualaddr >> 39) & 511;
	size_t pml3_entry = (virtualaddr >> 30) & 511;
	size_t pml2_entry = (virtualaddr >> 21) & 511;
	size_t pml1_entry = (virtualaddr >> 12) & 511;

	// Look in PML4.
	size_t *ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(kernel_pml4);
	if (pml4_entry != 511) {
		PrintString("Attempting to map kernel memory not in the last PML4 entry.");
		__asm__ __volatile__("hlt");
	}
	if(ptr[pml4_entry] == 0) {
		// If this entry is blank blank, create a PML3 table.
		size_t new_pml3 = GetPhysicalPagePreVirtualMemory();

		// Clear it.
		ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(new_pml3);
		size_t i;
		for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
			ptr[i] = 0;
		}

		// Switch back to the PML4.
		ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(kernel_pml4);

		// Write it in.
		ptr[pml4_entry] = new_pml3 | 0x1;

	}

	size_t pml3 = ptr[pml4_entry] & ~(PAGE_SIZE - 1);

	// Look in PML3.
	ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml3);
	if(ptr[pml3_entry] == 0) {
		// If this entry is blank, create a PML2 table.
		size_t new_pml2 = GetPhysicalPagePreVirtualMemory();

		// Clear it.
		ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(new_pml2);
		size_t i;
		for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
			ptr[i] = 0;
		}

		// Switch back to the PML3.
		ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml3);

		// Write it in.
		ptr[pml3_entry] = new_pml2 | 0x1;
	}

	size_t pml2 = ptr[pml3_entry] & ~(PAGE_SIZE - 1);

	if(assign_page_table) {
		// We're assigning a page table to the PML2 rather than a page to the PML1.
		ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml2);
		size_t entry = physicaladdr | 0x1;
		ptr[pml2_entry] = entry;
		return;
	}

	// Look in PML2.
	ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(pml2);
	if(ptr[pml2_entry] == 0) {
		// Entry blank, create a PML1 table.
		size_t new_pml1 = GetPhysicalPagePreVirtualMemory();

		// Clear it.
		ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(new_pml1);
		size_t i;
		for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
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

// Initializes the virtual allocator.
void InitializeVirtualAllocator() {
	// We entered long mode with a temporary setup, now it's time to build a real paging system for us.

	// Allocate a physical page to use as the kernel's PML4 and clear it.
	kernel_pml4 = GetPhysicalPagePreVirtualMemory();
	size_t *ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(kernel_pml4);
	size_t i;
	for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
		ptr[i] = 0;
	}

	// Figure out what is the start of free memory, past the loaded code.
	size_t start_of_free_kernel_memory = ((size_t)start_of_free_memory_at_boot + PAGE_SIZE - 1)
											& ~(PAGE_SIZE - 1); // Round up.

	// Map the booted code into memory.
	for(i = 0; i < start_of_free_kernel_memory; i += PAGE_SIZE)
		MapKernelMemoryPreVirtualMemory(i + VIRTUAL_MEMORY_OFFSET, i, false);
	i += VIRTUAL_MEMORY_OFFSET;

	// Allocate a virtual and physical page for our temporary page table.
	temp_memory_page_table = (size_t *)i;
	i += PAGE_SIZE;
	size_t physical_temp_memory_page_table = GetPhysicalPagePreVirtualMemory();
	MapKernelMemoryPreVirtualMemory((size_t)temp_memory_page_table, physical_temp_memory_page_table, false);

	// Maps the next 2MB range in memory for our temporary pages.
	size_t page_table_range = PAGE_SIZE * PAGE_TABLE_ENTRIES;
	temp_memory_start = (i + page_table_range) & ~(page_table_range - 1);

	MapKernelMemoryPreVirtualMemory(temp_memory_start, physical_temp_memory_page_table, true);

	// Set the assigned bit to each of the temporary page table entries so we don't think it's free to allocate stuff into.
	ptr = (size_t *)TemporarilyMapPhysicalMemoryPreVirtualMemory(physical_temp_memory_page_table);
	for(i = 0; i < PAGE_TABLE_ENTRIES; i++)
		ptr[i] = 1; // Assigned.

	// Flush and load the kernel's new and final PML4.
	current_pml4 = 1; // A dud entry so SwitchToAddressSpace works.
	SwitchToAddressSpace(kernel_pml4);

	// Reclaim the PML4, PDPT, PD set up at boot time.
	UnmapVirtualPage(kernel_pml4, (size_t)Pml4 + VIRTUAL_MEMORY_OFFSET, true);
	UnmapVirtualPage(kernel_pml4, (size_t)Pdpt + VIRTUAL_MEMORY_OFFSET, true);
	UnmapVirtualPage(kernel_pml4, (size_t)Pd + VIRTUAL_MEMORY_OFFSET, true);
}

// Maps a physical page so that we can access it - use this before the virtual allocator has been initialized.
void *TemporarilyMapPhysicalMemoryPreVirtualMemory(size_t addr) {
	// Round this down to the nearest 2MB as we use 2MB pages before we setup the virtual allocator.
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
	if(Pd[511] != entry) {
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


// Temporarily maps physical memory (page aligned) into virtual memory so we can fiddle with it.
// index is from 0 to 511 - mapping a different address to the same index unmaps the previous page mapped there.
void *TemporarilyMapPhysicalMemory(size_t addr, size_t index) {
	size_t entry = addr | 0x3;
	
	// Check if it's not already mapped.
	if(temp_memory_page_table[index] != entry) {
		// Map this page into our temporary page table.
		temp_memory_page_table[index] = entry;
		// Flush our page table cache.
		__asm__ __volatile__("mov %0, %%cr3":: "a"(current_pml4));
	}

	// Return a pointer to the virtual address of the requested physical memory.
	return (void *)(temp_memory_start + PAGE_SIZE * index);
}

// Finds a range of free physical pages in memory - returns the first address or OUT_OF_MEMORY if it can't find a fit.
size_t FindFreePageRange(size_t pml4, size_t pages) {
	if(pages == 0 || pages > 34359738368 /* 128 GB */) {
		// Too many or not enough entries.
		return 0; 
	}

	// Scan through each PML and store the first free entry we find.
	size_t start_pml1_entry = 0;
	size_t start_pml2_entry = 0;
	size_t start_pml3_entry = 0;
	size_t start_pml4_entry = 0;

	// Have we found an area and started counting?
	bool counting = false;
	// How many pages have we counted so far? Terminates when pages == pages_counted.
	size_t pages_counted = 0;

	size_t pml4_scan_start, pml4_scan_end;
	if(pml4 == kernel_pml4) {
		// For kernel space, scan the highest PML4 entry.
		pml4_scan_start = PAGE_TABLE_ENTRIES - 1;
		pml4_scan_end = PAGE_TABLE_ENTRIES;
	} else {
		// For user space, scan below kernel memory..
		pml4_scan_start = 0;
		pml4_scan_end = PAGE_TABLE_ENTRIES - 1;
	}

	// Scan the PML4.
	size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);

	size_t i;
	for(i = pml4_scan_start; i < pml4_scan_end && pages_counted < pages; i++) {
		if (i == PAGE_TABLE_ENTRIES / 2) {
			// There's a huge gap of non-canonical memory between
			// 0x00007FFFFFFFFFFF to 0xFFFF800000000000 that we can't use.
			counting = false;
			pages_counted = 0;
		}
		if(ptr[i] == 0) {
			if(!counting) {
				counting = true;
				start_pml1_entry = 0;
				start_pml2_entry = 0;
				start_pml3_entry = 0;
				start_pml4_entry = i;
			}
			pages_counted += PAGE_TABLE_ENTRIES * PAGE_TABLE_ENTRIES * PAGE_TABLE_ENTRIES;
		} else {
			// There's an entry.
			size_t pml3 = ptr[i] & ~(PAGE_SIZE - 1);

			// Scan PML3.
			ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
			size_t j;
			for(j = 0; j < PAGE_TABLE_ENTRIES && pages_counted < pages; j++) {
				if(ptr[j] == 0) {
					if(!counting) {
						counting = true;
						start_pml1_entry = 0;
						start_pml2_entry = 0;
						start_pml3_entry = j;
						start_pml4_entry = i;
					}
					pages_counted += PAGE_TABLE_ENTRIES * PAGE_TABLE_ENTRIES;
				} else {
					// There's an entry.
					size_t pml2 = ptr[j] & ~(PAGE_SIZE - 1);
					
					// Scan PML2.
					ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
					size_t k;
					for(k = 0; k < PAGE_TABLE_ENTRIES && pages_counted < pages; k++) {
						if(ptr[k] == 0) {
							if(!counting) {
								counting = true;
								start_pml1_entry = 0;
								start_pml2_entry = k;
								start_pml3_entry = j;
								start_pml4_entry = i;
							}
							pages_counted += PAGE_TABLE_ENTRIES;
						} else {
							// There's an entry.
							size_t pml1 = ptr[k] & ~(PAGE_SIZE - 1);

							// Scan PML1.
							ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
							size_t l;
							for(l = 0; l < PAGE_TABLE_ENTRIES && pages_counted < pages; l++) {
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
									// There's an entry.
									counting = false;
									pages_counted = 0;
								}
							}

							// Return the pointer to PML2.
							ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
						}
					}

					// Return the pointer to PML3.
					ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
				}
			}

			// Return the pointer to PML4.
			ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
		}
	}

	if(!counting || pages_counted < pages) {
		// We ran out of memory.
		PrintString("Failed: Counting: ");
		PrintNumber(counting);
		PrintString(" Pages Counted: ");
		PrintNumber(pages_counted);
		PrintString(" Pages: ");
		PrintNumber(pages);
		return OUT_OF_MEMORY;
	}


	// Cnvert each PML table index into an address.
	// 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	// 4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
    //                     #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
    //                     pml4       pml3       pml2       pml1        flags
	size_t addr = (start_pml4_entry << 39) | (start_pml3_entry << 30) | (start_pml2_entry << 21) | (start_pml1_entry << 12);
	
	// Make the address canonical.
	if(start_pml4_entry >= PAGE_TABLE_ENTRIES / 2)
		addr += 0xFFFF000000000000;

#ifdef DEBUG
	PrintString("Allocating ");
	PrintNumber(pages);
	PrintString(" at ");
	PrintHex(addr);
	PrintString("\n");
#endif

	// Return the virtual address we found.
	return addr;
}

// Maps a physical page to a virtual page. Returns if it was successful.
bool MapPhysicalPageToVirtualPage(size_t pml4, size_t virtualaddr, size_t physicaladdr, bool own) {
	// Find the index into each PML table.
	// 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	// 4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
    //                     #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
    //                     pml4       pml3       pml2       pml1        flags
	size_t pml4_entry = (virtualaddr >> 39) & 511;
	size_t pml3_entry = (virtualaddr >> 30) & 511;
	size_t pml2_entry = (virtualaddr >> 21) & 511;
	size_t pml1_entry = (virtualaddr >> 12) & 511;

	bool user_page = pml4_entry != PAGE_TABLE_ENTRIES - 1;

	if(pml4 == kernel_pml4) {
		// Kernel virtual addreses must in the highest pml4 entry.
		if(user_page) {
			return false;
		}
	} else {
		// User space virtual addresses must be below kernel memory.
		if(!user_page) {
			return false;
		}
	}

	bool created_pml3, created_pml2;

	// Look in PML4.
	size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
	if(ptr[pml4_entry] == 0) {
		// Entry blank, create a PML3 table.
		size_t new_pml3 = GetPhysicalPage();
		if(new_pml3 == OUT_OF_PHYSICAL_PAGES)
			return false; // No space for PML3.

		// Clear it.
		ptr = (size_t *)TemporarilyMapPhysicalMemory(new_pml3, 1);
		size_t i;
		for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
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
	if(ptr[pml3_entry] == 0) {
		// Entry blank, create a PML2 table.
		size_t new_pml2 = GetPhysicalPage();
		if(new_pml2 == OUT_OF_PHYSICAL_PAGES) {
			// No space for pml2.
			if(created_pml3) {
				ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
				ptr[pml4_entry] = 0;
				FreePhysicalPage((size_t)pml3);
			}
			return false;
		}

		// Clear it.
		ptr = (size_t *)TemporarilyMapPhysicalMemory(new_pml2, 2);
		size_t i;
		for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
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
	if(ptr[pml2_entry] == 0) {
		// Entry blank, create a PML1 table.
		size_t new_pml1 = GetPhysicalPage();
		if(new_pml1 == OUT_OF_PHYSICAL_PAGES) {
			// No space for PML1.
			if(created_pml2) {
				ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
				ptr[pml3_entry] = ((size_t)pml3);
				FreePhysicalPage((size_t)pml2);
			}
			
			if(created_pml3) {
				ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
				ptr[pml4_entry] = 0;
				FreePhysicalPage((size_t)pml3);
			}
			return false;
		}

		// Clear it.
		ptr = (size_t *)TemporarilyMapPhysicalMemory(new_pml1, 3);
		size_t i;
		for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
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
	if(ptr[pml1_entry] != 0) {
		// We don't worry about cleaning up PML2/3 because for it to be mapped PML2/3 must already exist.
		return false;
	}

	// Write us in PML1.
	size_t entry = physicaladdr | 0x3 |
		// Set the user bit.
		(user_page ? (1 << 2) : 0) |
		// Set the ownership bit (a custom bit.)
		(own ? (1 << 9) : 0);
	ptr[pml1_entry] = entry;


	if(created_pml3 && !user_page) {
		// We modified the kernel's PML4. Copy this into every user application's PML4.
		// TODO
	}

	if(pml4 == current_pml4 || !user_page) {
		// We need to flush the TLB because we are either in this address space or it's kernel memory (which we're always in
		// the address space of.)
		FlushVirtualPage(virtualaddr);
	}

	return true;
}

// Return the physical address mapped at a virtual address, returning OUT_OF_MEMORY if is not mapped.
size_t GetPhysicalAddress(size_t pml4, size_t virtualaddr) {
	size_t pml4_entry = (virtualaddr >> 39) & 511;
	size_t pml3_entry = (virtualaddr >> 30) & 511;
	size_t pml2_entry = (virtualaddr >> 21) & 511;
	size_t pml1_entry = (virtualaddr >> 12) & 511;

	if(pml4 == kernel_pml4) {
		// Kernel virtual addreses must in the last pml4 entry
		if(pml4_entry < PAGE_TABLE_ENTRIES - 1) {
			return OUT_OF_MEMORY;
		}
	} else {
		// User space virtual addresses must below kernel memory.
		if(pml4_entry == PAGE_TABLE_ENTRIES - 1) {
			return OUT_OF_MEMORY;
		}
	}

	// Look in PML4.
	size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
	if(ptr[pml4_entry] == 0) {
		// Entry blank.
		return OUT_OF_MEMORY;
	}

	// Look in PML3.
	size_t pml3 = ptr[pml4_entry] & ~(PAGE_SIZE - 1);
	ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
	if(ptr[pml3_entry] == 0) {
		// Entry blank.
		return OUT_OF_MEMORY;
	}

	// Look in PML2.
	size_t pml2 = ptr[pml3_entry] & ~(PAGE_SIZE - 1);
	ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);

	if(ptr[pml2_entry] == 0) {
		// Entry blank.
		return OUT_OF_MEMORY;
	}

	size_t pml1 = ptr[pml2_entry] & ~(PAGE_SIZE - 1);

	// Look in PML1.
	ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);;
	if(ptr[pml1_entry] == 0) {
		return OUT_OF_MEMORY;
	} else {
		return ptr[pml1_entry] & 0xFFFFFFFFFFFFF000;
	}
}

// Return the physical address mapped at a virtual address, returning OUT_OF_MEMORY if is not mapped.
size_t GetOrCreateVirtualPage(size_t pml4, size_t virtualaddr) {
	size_t physical_address = GetPhysicalAddress(pml4, virtualaddr);
	if (physical_address != OUT_OF_MEMORY) {
		return physical_address;
	}

	physical_address = GetPhysicalPage();
	if (physical_address == OUT_OF_PHYSICAL_PAGES) {
		return OUT_OF_MEMORY;
	}

	if(MapPhysicalPageToVirtualPage(pml4, virtualaddr, physical_address, true)) {
		return physical_address;
	} else {
		FreePhysicalPage(physical_address);
		return OUT_OF_MEMORY;
	}
}


size_t AllocateVirtualMemoryInAddressSpace(size_t pml4, size_t pages) {
	size_t start = FindFreePageRange(pml4, pages);
	if(start == OUT_OF_MEMORY) {
		return 0;
	}

	// Allocate each page we've found.
	size_t addr = start;
	size_t i;
	for(i = 0; i < pages; i++, addr += PAGE_SIZE) {
		// Get a physical page.
		size_t phys = GetPhysicalPage();

		if(phys == OUT_OF_PHYSICAL_PAGES) {
			// No physical pages. Unmap all memory up until this point.
			for(;start < addr; start += PAGE_SIZE) {
				UnmapVirtualPage(pml4, start, true);
			}
			return 0;
		}

		// Map the physical page.
		MapPhysicalPageToVirtualPage(pml4, addr, phys, true);

		if (current_pml4 == pml4) {
			FlushVirtualPage(addr);
		}
	}

	return start;
}

size_t ReleaseVirtualMemoryInAddressSpace(size_t pml4, size_t addr, size_t pages) {
	size_t i = 0;
	for(;i < pages; i++, addr += PAGE_SIZE) {
		UnmapVirtualPage(pml4, addr, true);
	}
}


size_t MapPhysicalMemoryInAddressSpace(size_t pml4, size_t addr, size_t pages) {
	size_t start_virtual_address = FindFreePageRange(pml4, pages);
	if (start_virtual_address == OUT_OF_MEMORY)
		return OUT_OF_MEMORY;

	for (size_t virtual_address = start_virtual_address;
		pages > 0; pages--, virtual_address += PAGE_SIZE, addr += PAGE_SIZE) {
#ifdef DEBUG
		PrintString("Mapping ");
		PrintHex(addr);
		PrintString(" to ");
		PrintHex(virtual_address);
		PrintString("\n");
#endif
		MapPhysicalPageToVirtualPage(pml4, virtual_address, addr, false);
	}
	return start_virtual_address;

}

// Unmaps a virtual page - free specifies if that page should be returned to the physical memory manager.
void UnmapVirtualPage(size_t pml4, size_t virtualaddr, bool free) {
	// Find the index into each PML table.
	// 6666 5555 5555 5544 4444 4444 4333 3333 3332 2222 2222 2111 1111 111
	// 4321 0987 6543 2109 8765 4321 0987 6543 2109 8765 4321 0978 6543 2109 8765 4321
    //                     #### #### #@@@ @@@@ @@!! !!!! !!!+ ++++ ++++ ^^^^ ^^^^ ^^^^
    //                     pml4       pml3       pml2       pml1        flags

	size_t pml4_entry = (virtualaddr >> 39) & 511;
	size_t pml3_entry = (virtualaddr >> 30) & 511;
	size_t pml2_entry = (virtualaddr >> 21) & 511;
	size_t pml1_entry = (virtualaddr >> 12) & 511;

	if(pml4 == kernel_pml4) {
		// Kernel virtual addresses must in the last PML4 entry.
		if(pml4_entry < PAGE_TABLE_ENTRIES - 1) {
			return;
		}
	} else {
		// User space virtual addresses must below kernel memory.
		if(pml4_entry == PAGE_TABLE_ENTRIES - 1) {
			return;
		}
	}

	// Look in PML4.
	size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
	if(ptr[pml4_entry] == 0) {
		// This address isn't mapped.
		return;
	}

	size_t pml3 = ptr[pml4_entry] & ~(PAGE_SIZE - 1);

	// Look in PML3.
	ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
	if(ptr[pml3_entry] == 0) {
		// This address isn't mapped.
		return;
	}

	size_t pml2 = ptr[pml3_entry] & ~(PAGE_SIZE - 1);

	// Look in PML2.
	ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
	if(ptr[pml2_entry] == 0) {
		// This address isn't mapped.
		return;
	}

	size_t pml1 = ptr[pml2_entry] & ~(PAGE_SIZE - 1);

	// Look in PML1.
	ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
	if(ptr[pml1_entry] == 0) {
		// This address isn't mapped.
		return;
	}

	// This adddress was mapped somwhere.

	// Should we free it, and it owned by this process?
	if(free && (ptr[pml1_entry] & (1 << 9)) == 0) {
		// Return the memory to the physical allocator. This is optional because we don't want to do this if it's shared or
		// memory mapped IO.

		// Figure out the physical address that this entry points to and free it.
		size_t physicaladdr = ptr[pml1_entry] & ~(PAGE_SIZE - 1);
		FreePhysicalPage(physicaladdr);

		// Load the PML1 again incase FreePhysicalPage maps something else.
		ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
	}

	// Remove this entry from the PML1.
	ptr[pml1_entry] = 0;

	// Scan to see if we find anything in the PML tables. If not, we can free them.
	bool found = 0;
	size_t i;
	// Scan PML1.
	for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
		if(ptr[i] != 0) {
			found = 1;
			break;
		}
	}

	if(!found) {
		// There was nothing in the PML1. We can free it.
		FreePhysicalPage(pml1);

		// Remove from the entry from the PML2.
		ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
		ptr[pml2_entry] = 0;

		// Scan the PML2.
		for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
			if(ptr[i] != 0) {
				found = 1;
				break;
			}
		}

		if(!found) {
			// There was nothing in the PML2. We can free it.
			FreePhysicalPage(pml2);

			// Remove the entry from the PML3.
			ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
			ptr[pml3_entry] = 0;

			// Scan the PML3.
			for(i = 0; i < PAGE_TABLE_ENTRIES; i++) {
				if(ptr[i] != 0) {
					found = 1;
					break;
				}
			}

			if(!found) {
				// There was nothing in the PML3. We can free it.
				FreePhysicalPage(pml3);

				// Remove the entry from the PML4.
				ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
				ptr[pml4_entry] = 0;
			}
		}

	}

	if(pml4 == current_pml4 || pml4_entry >= PAGE_TABLE_ENTRIES - 1) {
		// Flush the TLB if we are in this address space or if it's a kernel page.
		FlushVirtualPage(virtualaddr);
	}
}


// Creates a process's virtual address space, returns the PML4. Returns OUT_OF_MEMORY if it fails.
size_t CreateAddressSpace() {
	size_t pml4 = GetPhysicalPage();
	if(pml4 == OUT_OF_PHYSICAL_PAGES) {
		return OUT_OF_MEMORY;
	}

	// Clear out this virtual address space.
	size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
	size_t i;
	for(i = 0; i < PAGE_TABLE_ENTRIES - 1; i++) {
		ptr[i] = 0;
	}

	// Copy the kernel's address space into this.
	size_t *kernel_ptr = (size_t *)TemporarilyMapPhysicalMemory(kernel_pml4, 1);
	ptr[PAGE_TABLE_ENTRIES - 1] = kernel_ptr[PAGE_TABLE_ENTRIES - 1];
	
	return pml4;
}

// Frees an address space. Everything it finds will be returned to the physical allocator so unmap any shared memory before. Please
// don't pass it the kernel's PML4.
void FreeAddressSpace(size_t pml4) {
	// If we're working in this address space, switch to kernel space.
	if(current_pml4 == pml4) {
		SwitchToAddressSpace(kernel_pml4);
	}

	// Scan the lower half of PML4.
	size_t *ptr = (size_t *)TemporarilyMapPhysicalMemory(pml4, 0);
	size_t i;
	for(i = 0; i < PAGE_TABLE_ENTRIES - 1; i++) {
		if(ptr[i] != 0) {
			// Found a PML3.
			size_t pml3 = ptr[i] & ~(PAGE_SIZE - 1);

			// Scan the PML3.
			ptr = (size_t *)TemporarilyMapPhysicalMemory(pml3, 1);
			size_t j;
			for(j = 0; j < PAGE_TABLE_ENTRIES; j++) {
				if(ptr[j] != 0) {
					// Found a PML2.
					size_t pml2 = ptr[j] & ~(PAGE_SIZE - 1);

					// Scan the PML2.
					ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);
					size_t k;
					for(k = 0; k < PAGE_TABLE_ENTRIES; k++) {
						if(ptr[k] != 0) {
							// Found a PML1.
							size_t pml1 = ptr[k] & ~(PAGE_SIZE - 1);

							// Scan the PML1.
							ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
							size_t l;
							for(l = 0; l < PAGE_TABLE_ENTRIES; l++) {
								if(ptr[l] != 0) {
									// We found a page, find it's physical address and free it.
									size_t physicaladdr = ptr[l] & ~(PAGE_SIZE - 1);
									FreePhysicalPage(physicaladdr);

									// Make sure the PML1 is mapped in memory after calling FreePhysicalPage.
									ptr = (size_t *)TemporarilyMapPhysicalMemory(pml1, 3);
								}
							}

							// Free the PML1.
							FreePhysicalPage(pml1);

							// Make sure the PML2 is mapped in memory after calling FreePhysicalPage.
							ptr = (size_t *)TemporarilyMapPhysicalMemory(pml2, 2);

						}
					}

					// Free the PML2.
					FreePhysicalPage(pml2);

					// Make sure the PML3 is mapped in memory after calling FreePhysicalPage.
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
}

// Switch to a virtual address space.
void SwitchToAddressSpace(size_t pml4) {
	if(pml4 != current_pml4) {
		current_pml4 = pml4;
		__asm__ __volatile__("mov %0, %%cr3":: "b"(pml4));
	}
}

// Flush the CPU lookup for a particular virtual address.
void FlushVirtualPage(size_t addr) {
	//*SwitchToAddressSpace(current_pml4);*/

	/*
	This gives me an assembler error:
		Error: junk `(%rbp))' after expression*/
	 __asm__ __volatile__ ( "invlpg (%0)" : : "b"(addr) : "memory" );
}


// Maps shared memory into a process's virtual address space. Returns NULL if
// there was an issue.
struct SharedMemoryInProcess* MapSharedMemoryIntoProcess(
	struct Process* process, struct SharedMemory* shared_memory) {

	// Find a free page range to map this shared memory into.
	size_t virtual_address = FindFreePageRange(
		process->pml4, shared_memory->size_in_pages);
	if (virtual_address == OUT_OF_MEMORY) {
		// No space to allocate these pages to!
		return NULL;
	}

	struct SharedMemoryInProcess* shared_memory_in_process =
		AllocateSharedMemoryInProcess();
	if (shared_memory_in_process == NULL) {
		// Out of memory.
		return NULL;
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
	shared_memory_in_process->virtual_address = virtual_address;
	shared_memory_in_process->references = 1;

	// The next shared memory block in the process.
	struct SharedMemoryInProcess* next_in_process;

	// Add it to our linked list.
	shared_memory_in_process->next_in_process = process->shared_memory;
	process->shared_memory = shared_memory_in_process;

	// Map the physical pages into memory.
	struct SharedMemoryPage* shared_memory_page = shared_memory->first_page;
	while (shared_memory_page != NULL) {
		// Map the physical page to the virtual address.
		MapPhysicalPageToVirtualPage(process->pml4,
			virtual_address, shared_memory_page->physical_address, false);

		// Iterate to the next page.
		virtual_address += PAGE_SIZE;
		shared_memory_page = shared_memory_page->next;
	}

	return shared_memory_in_process;
}

// Unmaps shared memory from a process and releases the SharedMemoryInProcess
// object.
void UnmapSharedMemoryFromProcess(struct Process* process,
	struct SharedMemoryInProcess* shared_memory_in_process) {

#ifdef DEBUG
	PrintString("Process ");
	PrintNumber(process->pid);
	PrintString(" is leaving shared memory.\n");
#endif

	// Unmap the virtual pages.
	ReleaseVirtualMemoryInAddressSpace(process->pml4,
		shared_memory_in_process->virtual_address,
		shared_memory_in_process->shared_memory->size_in_pages);

	// Remove from linked list in the process.
	if (process->shared_memory == shared_memory_in_process) {
		// First element in the linked list.
		process->shared_memory = shared_memory_in_process->next_in_process;
	} else {
		// Iterate through until we find it.
		struct SharedMemoryInProcess* previous = process->shared_memory;
		while (previous != NULL &&
			previous->next_in_process != shared_memory_in_process) {
			previous = previous->next_in_process;
		}

		if (previous == NULL) {
			PrintString("Shared memory can't be unmapped from a process that "
				"it's not mapped to.\n");
			return;
		}

		// Remove us from the linked list.
		previous->next_in_process = shared_memory_in_process->next_in_process;
	}


	// Decrement the references to this shared memory block.
	shared_memory_in_process->shared_memory->
		processes_referencing_this_block--;
	if (shared_memory_in_process->shared_memory->
			processes_referencing_this_block == 0) {
		// There are no more refernces to this shared memory block, so we can
		// release the memory.
		ReleaseSharedMemoryBlock(shared_memory_in_process->shared_memory);
	}

	ReleaseSharedMemoryInProcess(shared_memory_in_process);
}