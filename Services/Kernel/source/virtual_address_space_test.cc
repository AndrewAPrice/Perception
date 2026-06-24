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

#include "virtual_address_space.h"

#include <unordered_map>

#include "process.h"
#include "testing.h"
#include "virtual_allocator.h"
#include "object_pools.h"

struct PhysicalPageBuffer {
  size_t entries[512];
};

extern std::unordered_map<size_t, PhysicalPageBuffer> simulated_ram;
extern size_t mock_cr3;

TEST(VirtualAddressSpacePagingTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  // Create a user process
  Process* proc = CreateProcess(false, false);
  ASSERT(proc != nullptr, true);

  // Switch to the user address space to load its PML4
  proc->virtual_address_space.SwitchToAddressSpace();
  size_t user_pml4 = mock_cr3;
  ASSERT(user_pml4 != 0, true);

  // Map virtual address 0x400000 to physical address 0x9000000
  size_t virtual_addr = 0x400000;
  size_t physical_addr = 0x9000000;
  bool success = proc->virtual_address_space.MapPhysicalPageAt(
      virtual_addr, physical_addr, /*own=*/true, /*can_write=*/true,
      /*throw_exception_on_access=*/false);

  ASSERT(success, true);

  // Verify PML4 (Level 4)
  // virtual_addr index in PML4: (0x400000 >> 39) & 0x1FF => 0
  size_t pml4_entry = simulated_ram[user_pml4].entries[0];
  ASSERT((pml4_entry & 1), (size_t)1);  // Present bit

  // Verify PDPT (Level 3)
  size_t pdpt_phys = pml4_entry & ~0xFFF;
  ASSERT(simulated_ram.find(pdpt_phys) != simulated_ram.end(), true);
  // virtual_addr index in PDPT: (0x400000 >> 30) & 0x1FF => 0
  size_t pdpt_entry = simulated_ram[pdpt_phys].entries[0];
  ASSERT((pdpt_entry & 1), (size_t)1);  // Present bit

  // Verify PD (Level 2)
  size_t pd_phys = pdpt_entry & ~0xFFF;
  ASSERT(simulated_ram.find(pd_phys) != simulated_ram.end(), true);
  // virtual_addr index in PD: (0x400000 >> 21) & 0x1FF => 2
  size_t pd_entry = simulated_ram[pd_phys].entries[2];
  ASSERT((pd_entry & 1), (size_t)1);  // Present bit

  // Verify PT (Level 1)
  size_t pt_phys = pd_entry & ~0xFFF;
  ASSERT(simulated_ram.find(pt_phys) != simulated_ram.end(), true);
  // virtual_addr index in PT: (0x400000 >> 12) & 0x1FF => 0
  size_t pt_entry = simulated_ram[pt_phys].entries[0];
  ASSERT((pt_entry & 1), (size_t)1);  // Present bit

  // Verify final physical mapping and flags
  size_t mapped_phys = pt_entry & ~0xFFF;
  ASSERT(mapped_phys, physical_addr);  // Must map to our physical address

  ASSERT((pt_entry & 2) != 0, true);         // Writable bit
  ASSERT((pt_entry & 4) != 0, true);         // UserSpace bit
  ASSERT((pt_entry & (1 << 9)) != 0, true);  // Owned bit
}

TEST(VirtualAddressSpaceTrackingTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  // Create a user process
  Process* proc = CreateProcess(false, false);
  ASSERT(proc != nullptr, true);

  proc->virtual_address_space.SwitchToAddressSpace();

  // Initially unique and shared pages are 0
  ASSERT(proc->virtual_address_space.GetUniquePages(), (size_t)0);
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)0);

  // Map virtual address 0x400000 to physical address 0x9000000 (owned/unique)
  bool success = proc->virtual_address_space.MapPhysicalPageAt(
      0x400000, 0x9000000, /*own=*/true, /*can_write=*/true,
      /*throw_exception_on_access=*/false);
  ASSERT(success, true);
  ASSERT(proc->virtual_address_space.GetUniquePages(), (size_t)1);
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)0);

  // Map virtual address 0x401000 to physical address 0x9001000 (unowned/shared)
  success = proc->virtual_address_space.MapPhysicalPageAt(
      0x401000, 0x9001000, /*own=*/false, /*can_write=*/true,
      /*throw_exception_on_access=*/false);
  ASSERT(success, true);
  ASSERT(proc->virtual_address_space.GetUniquePages(), (size_t)1);
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)1);

  // Map virtual address 0x402000 as a dud/lazy page (shared)
  success = proc->virtual_address_space.MapPhysicalPageAt(
      0x402000, 0, /*own=*/false, /*can_write=*/false,
      /*throw_exception_on_access=*/true);
  ASSERT(success, true);
  ASSERT(proc->virtual_address_space.GetUniquePages(), (size_t)1);
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)2);

  // Replace dud page with a real page (still shared)
  success = proc->virtual_address_space.MapPhysicalPageAt(
      0x402000, 0x9002000, /*own=*/false, /*can_write=*/true,
      /*throw_exception_on_access=*/false);
  ASSERT(success, true);
  ASSERT(proc->virtual_address_space.GetUniquePages(), (size_t)1);
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)2);

  // Unmap 0x400000 (unique page)
  proc->virtual_address_space.FreePages(0x400000, 1);
  ASSERT(proc->virtual_address_space.GetUniquePages(), (size_t)0);
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)2);

  // Unmap 0x401000 (shared page)
  proc->virtual_address_space.ReleasePages(0x401000, 1);
  ASSERT(proc->virtual_address_space.GetUniquePages(), (size_t)0);
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)1);

  // Unmap 0x402000 (shared page)
  proc->virtual_address_space.ReleasePages(0x402000, 1);
  ASSERT(proc->virtual_address_space.GetUniquePages(), (size_t)0);
  ASSERT(proc->virtual_address_space.GetSharedPages(), (size_t)0);
}
