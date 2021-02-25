// Copyright 2021 Google LLC
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

#include "object_pools.h"
#include "physical_allocator.h"
#include "process.h"
#include "shared_memory.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

// The last assigned shared memory ID.
size_t last_assigned_shared_memory_id;

// Linked list of all shared memory blocks.
struct SharedMemory* first_shared_memory;

// Initializes the internal structures for shared memory.
void InitializeSharedMemory() {
	last_assigned_shared_memory_id = 0;
	first_shared_memory = NULL;
}

// Creates a shared memory block.
struct SharedMemory* CreateSharedMemoryBlock(size_t pages) {
	struct SharedMemory* shared_memory = AllocateSharedMemory();
	if (shared_memory == NULL) {
		return NULL;
	}

	last_assigned_shared_memory_id++;
	shared_memory->id = last_assigned_shared_memory_id;
	shared_memory->size_in_pages = pages;
	shared_memory->processes_referencing_this_block = 0;
	shared_memory->first_page = NULL;
	shared_memory->next = NULL;
	shared_memory->previous = NULL;

	// Allocate each page.
	struct SharedMemoryPage* last_shared_memory_page = NULL;

	while (pages > 0) {
		size_t physical_page = GetPhysicalPage();
		if (physical_page == OUT_OF_PHYSICAL_PAGES) {
			// Out of memory.
			ReleaseSharedMemoryBlock(shared_memory);
			return NULL;
		}

		struct SharedMemoryPage* shared_memory_page =
			AllocateSharedMemoryPage();
		if (shared_memory_page == NULL) {
			// Out of memory.
			FreePhysicalPage(physical_page);
			ReleaseSharedMemoryBlock(shared_memory);
			return NULL;
		}

		shared_memory_page->physical_address = physical_page;
		shared_memory_page->next = NULL;

		// Add to the linked list of pages.
		if (last_shared_memory_page == NULL) {
			shared_memory->first_page = shared_memory_page;
		} else {
			last_shared_memory_page->next = shared_memory_page;
		}
		last_shared_memory_page = shared_memory_page;

		pages--;
	}

	if (first_shared_memory != NULL) {
		shared_memory->next = first_shared_memory;
	}
	first_shared_memory = shared_memory;

	return shared_memory;
}


// Creates a shared memory block and map it into a procses.
struct SharedMemoryInProcess* CreateAndMapSharedMemoryBlockIntoProcess(
	struct Process* process, size_t pages) {
	// Create the shared memory block.
	struct SharedMemory* shared_memory = CreateSharedMemoryBlock(pages);
	if (shared_memory == NULL) {
		// Could not create shared memory.
		return NULL;
	}

	// Map it into this process.
	struct SharedMemoryInProcess* shared_memory_in_process =
		MapSharedMemoryIntoProcess(process, shared_memory);
	if (shared_memory_in_process == NULL) {
		ReleaseSharedMemoryBlock(shared_memory);
	}
	return shared_memory_in_process;
}

// Releases a shared memory block. Please make sure that there are no
// processes referencing the shared memory block before calling this.
void ReleaseSharedMemoryBlock(struct SharedMemory* shared_memory) {
	if (shared_memory->processes_referencing_this_block > 0) {
		PrintString("Attempting to release shared memory that still is being "
			"referenced by a process.\n");
		return;
	}

	// Release each physical page associated with this shared memory block.
	struct SharedMemoryPage* next_shared_memory_page =
		shared_memory->first_page;
	while (next_shared_memory_page != NULL) {
		// Save our pointer and iterate to the next shared memory page.
		struct SharedMemoryPage* page = next_shared_memory_page;
		next_shared_memory_page = next_shared_memory_page->next;

		// Release this physical page.
		FreePhysicalPage(page->physical_address);

		// Release this SharedMemoryPage object.
		ReleaseSharedMemoryPage(page);
	}

	// Remove us from the linked list of shared memory.
	if (shared_memory->next != NULL) {
		shared_memory->next->previous = shared_memory->previous;
	}

	if (shared_memory->previous == NULL) {
		first_shared_memory = shared_memory->next;
	} else {
		shared_memory->previous->next = shared_memory->next;
	}

	// Release the SharedMemory object.
	ReleaseSharedMemory(shared_memory);
}

struct SharedMemory* GetSharedMemoryFromId(size_t shared_memory_id) {
	struct SharedMemory* shared_memory = first_shared_memory;
	while (shared_memory != NULL) {
		if (shared_memory->id == shared_memory_id) {
			// Found a shared memory block that matches the ID.
			return shared_memory;
		}
		shared_memory = shared_memory->next;
	}

	// Can't find any shared memory block with this ID.
	return NULL;
}

// Joins a shared memory block. Ensures that a shared memory is only mapped once
// per process. Returns the virtual address of the shared memory block or 0.
struct SharedMemoryInProcess* JoinSharedMemory(
	struct Process* process, size_t shared_memory_id) {
	// See if this shared memory is already mapped into this process.
	struct SharedMemoryInProcess* shared_memory_in_process =
		process->shared_memory;
	while (shared_memory_in_process != NULL) {
		if (shared_memory_in_process->shared_memory->id == shared_memory_id) {
			// This shared memory is already mapped into the process.
			shared_memory_in_process->references++;
			return shared_memory_in_process;
		}
		shared_memory_in_process = shared_memory_in_process->next_in_process;
	}

	// The shared memory is not mapped to the process, so we'll try to find it.
	struct SharedMemory* shared_memory = GetSharedMemoryFromId(shared_memory_id);
	if (shared_memory == NULL) {
		// No shared memory with this ID exists.
		return 0;
	}

	// Map this shared memory in this process.
	return MapSharedMemoryIntoProcess(process, shared_memory);
}

// Leaves a shared memory block, but doesn't unmap it if there are still other
// referenes to the shared memory block in the process.
void LeaveSharedMemory(struct Process* process, size_t shared_memory_id) {
	// Find the shared memory.
	struct SharedMemoryInProcess* shared_memory_in_process =
		process->shared_memory;
	while (shared_memory_in_process != NULL) {
		if (shared_memory_in_process->shared_memory->id == shared_memory_id) {
			// Found the shared memory block.
			shared_memory_in_process->references--;

			if (shared_memory_in_process == 0) {
				// No more references to this shared memory, so we can unmap
				// it.
				UnmapSharedMemoryFromProcess(process,
					shared_memory_in_process);
			}

			return;
		}
		shared_memory_in_process = shared_memory_in_process->next_in_process;
	}
}