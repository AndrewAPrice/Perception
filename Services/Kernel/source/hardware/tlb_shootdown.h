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

#include "hardware/current_core.h"
#include "types.h"

// Cross-core TLB invalidation.
//
// The kernel never runs with interrupts enabled, so a remote core executing
// kernel code cannot take the shootdown IPI until it returns to user space.
// Waiting for such a core to acknowledge would deadlock if it is blocked on a
// lock the sender holds. Instead every spin loop in the kernel calls
// `PollTlbShootdown()`, so a core that is waiting for anything also services
// shootdowns. The IPI then only exists to interrupt cores running in user
// space, which do have interrupts enabled.
//
// This header deliberately sits below `containers/`, depending only on
// `hardware/current_core.h`, so that `containers/spinlock.h` can poll from its
// acquire loop without creating a circular dependency.
namespace hardware {

// Shootdown address meaning "invalidate the entire TLB" rather than one page.
// Not a valid page address, because it is not page aligned.
constexpr size_t kFlushEntireTlb = ~static_cast<size_t>(0);

// CR4 bit enabling global pages, which survive a CR3 reload.
constexpr size_t kCr4PageGlobalEnable = 1 << 7;

// The address the in-flight shootdown is invalidating, or kFlushEntireTlb.
// Only meaningful while a bit is set in g_shootdown_pending_mask.
extern size_t g_shootdown_address;

// One bit per core that has not yet acknowledged the in-flight shootdown.
extern uint64 g_shootdown_pending_mask;

// Invalidates every TLB entry on this core, including global pages. Kernel
// pages are mapped global and survive a CR3 reload, so CR4.PGE is toggled
// instead.
inline void FlushEntireTlbIncludingGlobalPages() {
#ifndef TEST
  size_t cr4;
  asm volatile("mov %%cr4, %0" : "=r"(cr4));
  asm volatile("mov %0, %%cr4" ::"r"(cr4 & ~kCr4PageGlobalEnable) : "memory");
  asm volatile("mov %0, %%cr4" ::"r"(cr4) : "memory");
#endif
}

// Services a TLB shootdown targeted at this core, if one is pending. Safe to
// call from any spin loop: it takes no locks and only touches this core's bit.
inline void PollTlbShootdown() {
#ifndef TEST
  uint64 pending = __atomic_load_n(&g_shootdown_pending_mask, __ATOMIC_ACQUIRE);
  uint64 core_bit = 1ULL << GetCurrentCoreId();
  if ((pending & core_bit) == 0) return;

  size_t address = __atomic_load_n(&g_shootdown_address, __ATOMIC_ACQUIRE);
  if (address == kFlushEntireTlb)
    FlushEntireTlbIncludingGlobalPages();
  else
    asm volatile("invlpg (%0)" ::"r"(address) : "memory");

  __atomic_fetch_and(&g_shootdown_pending_mask, ~core_bit, __ATOMIC_RELEASE);
#endif
}

}  // namespace hardware
