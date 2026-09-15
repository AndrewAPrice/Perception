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

// The physical allocator manages physical memory, and operates by grabbing and
// freeing pages (4 KB chunks of memory).

#include "types.h"

namespace memory {

// The total number of bytes of system memory.
extern size_t g_total_system_memory;

// The total number of free pages.
extern size_t g_free_pages;

// Start of free memory at boot.
extern size_t g_start_of_free_memory_at_boot;

// The size of a page in bytes (4 KB). Changing this will probably break the
// virtual allocator.
constexpr size_t kPageSize = 4096;

// Magic value when physical pages are exhausted. Sits at the top of the 64 bit
// range so it can't collide with a real page (0 is a valid page), and is
// distinct from kError and kOutOfMemory so the three can be told apart.
constexpr size_t kOutOfPhysicalPages = ~static_cast<size_t>(0) - 2;

// Initializes the physical allocator.
void InitializePhysicalAllocator();

// Indicates that multiboot memory is no longer needed and can be released.
void DoneWithMultibootMemory();

// Grabs the next physical page (at boot time before the virtual memory
// allocator is initialized), returns kOutOfPhysicalPages if there are no more
// physical pages.
size_t GetPhysicalPagePreVirtualMemory();

// Grabs the next physical page, returns kOutOfPhysicalPages if there are no
// more physical pages.
size_t GetPhysicalPage();

// Grabs the next physical page starting at or below the provided physical
// address, returns kOutOfPhysicalPages if there are no more physical pages.
size_t GetPhysicalPageAtOrBelowAddress(size_t max_base_address);

// Frees a physical page.
void FreePhysicalPage(size_t addr);

// Returns whether an address is the start of a memory page.
bool IsPageAlignedAddress(size_t address);

// Rounds an address down to the start of the page that it's in.
size_t RoundDownToPageAlignedAddress(size_t address);

// Rounds an address up to the start of the next page boundary (or itself if
// already aligned).
inline size_t RoundUpToPageAlignedAddress(size_t address) {
  return (address + kPageSize - 1) & ~(kPageSize - 1);
}

// Returns the byte offset of an address within its page.
inline size_t PageOffset(size_t address) { return address & (kPageSize - 1); }

}  // namespace memory


