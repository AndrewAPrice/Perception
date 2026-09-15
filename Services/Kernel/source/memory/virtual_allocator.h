// Copyright 2026 Google LLC
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

// The virtual allocator manages virtual memory, with variations of the
// functions for managing userland and kernelland memory. Virtual address spaces
// are identified by the PML4 address being passed around. The kernel has a
// PML4, and each running process will have its own PML4.

// Some information on different PML levels: http://wiki.osdev.org/Page_Tables

#include "types.h"

namespace processes {
struct Process;
}
namespace ipc {
struct SharedMemoryInProcess;
struct SharedMemory;
}
namespace memory {

class VirtualAddressSpace;

// The offset from physical to virtual memory (0x8000000000 = 256gb).
constexpr size_t kVirtualMemoryOffset = 0xFFFFFFFF80000000ULL;

// Initializes the virtual allocator.
void InitializeVirtualAllocator();

// The kernel's virtual address space.
VirtualAddressSpace &KernelAddressSpace();

// Flush the CPU lookup for a particular virtual address.
void FlushVirtualPage(size_t addr);

// Maps a physical page so that it can be accessed before the virtual
// allocator has been initialized. Returns a pointer to the page in virtual
// memory space. Only one page at a time can be allocated this way. The index
// is ignored but is used to match the function definition of
// `TemporarilyMapPhysicalMemory`.
void *TemporarilyMapPhysicalMemoryPreVirtualMemory(size_t addr, size_t index);

// Temporarily maps physical memory (page aligned) into virtual memory for
// manipulation. Index is from 0 to 511 - mapping a different address to the
// same index unmaps the previous page mapped there.
void *TemporarilyMapPhysicalPages(size_t addr, size_t index);

// Maps shared memory into a process's virtual address space. Returns nullptr if
// there was an issue.
ipc::SharedMemoryInProcess *MapSharedMemoryIntoProcess(processes::Process *process,
                                                  ipc::SharedMemory *shared_memory);

// Maps shared memory into a process's virtual address space starting at the
// given virtual address. Returns nullptr if there was an issue. Make sure
// FindAndReserveFreePageRange or ReserveAddressRange was called for the address
// range before calling this.
ipc::SharedMemoryInProcess *MapSharedMemoryIntoProcessAtAddress(
    processes::Process *process, ipc::SharedMemory *shared_memory, size_t virtual_address);

// Unmaps shared memory from a process and releases the SharedMemoryInProcess
// object.
void UnmapSharedMemoryFromProcess(
    ipc::SharedMemoryInProcess *shared_memory_in_process);

// Returns the non-canonical hole in virtual memory.
void GetUserspaceVirtualMemoryHole(size_t &hole_start_address,
                                   size_t &hole_end_address, bool inclusive);

}  // namespace memory


