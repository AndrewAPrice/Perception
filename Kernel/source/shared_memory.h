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

#pragma once

struct Process;

// Represents a linked list of physical pages that makes up a shared
// memory block.
struct SharedMemoryPage {
	// The physical address of this page.
	size_t physical_address;

	// Pointer to the next SharedMemoryPage block, otherwise 0 if there
	// are no more pages.
	struct SharedMemoryPage* next;
};

// Represents a block of shared memory.
struct SharedMemory {
	// The ID of this shared memory.
	size_t id;

	// The size of this shared memory block, in pages.
	size_t size_in_pages;

	// Linked list of physical pages.
	struct SharedMemoryPage* first_page;

	// Number of processes that are referencing this block.
	size_t processes_referencing_this_block;

	// Linked list of shared memory.
	struct SharedMemory* previous;
	struct SharedMemory* next;
};

// Represents a block of shared memory mapped into a proces.
struct SharedMemoryInProcess {
	// The shared memory block we're talking about.
	struct SharedMemory* shared_memory;

	// The virtual address of this shared memory block.
	size_t virtual_address;

	// The next shared memory block in the process.
	struct SharedMemoryInProcess* next_in_process;

	// The number of references to this shared memory block in this process.
	size_t references;
};

// Initializes the internal structures for shared memory.
extern void InitializeSharedMemory();

// Creates a shared memory block and map it into a procses.
extern struct SharedMemoryInProcess* CreateAndMapSharedMemoryBlockIntoProcess(
	struct Process* process, size_t pages);

// Releases a shared memory block. Please make sure that there are no
// processes referencing the shared memory block before calling this.
extern void ReleaseSharedMemoryBlock(struct SharedMemory* shared_memory);

// Joins a shared memory block. Ensures that a shared memory is only mapped once
// per process.
extern struct SharedMemoryInProcess* JoinSharedMemory(
	struct Process* process, size_t shared_memory_id);

// Leaves a shared memory block, but doesn't unmap it if there are still other
// referenes to the shared memory block in the process.
extern void LeaveSharedMemory(struct Process* process, size_t shared_memory_id);


