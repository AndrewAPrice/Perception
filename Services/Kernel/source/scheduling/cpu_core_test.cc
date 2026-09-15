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

#include "scheduling/cpu_core.h"
#include "testing.h"

using scheduling::CpuCoreState;
using scheduling::g_cpu_cores;
using scheduling::g_idle_cores_mask;
using scheduling::g_interrupt_stacks;
using scheduling::g_online_cores_mask;
using scheduling::InitializeCpuCoreState;
using scheduling::kKernelCodeSelector;
using scheduling::kKernelDataSelector;
using scheduling::kTssGdtSelector;
using scheduling::kUserDataSelector;
using scheduling::kUserCodeSelector;
using hardware::TaskStateSegment;

TEST(CpuCoreInitialization) {
  InitializeCpuCoreState(0, 0);

  CpuCoreState& cpu0 = g_cpu_cores[0];
  ASSERT(cpu0.self_pointer, &cpu0);
  ASSERT(cpu0.core_id, (uint32)0);
  ASSERT(cpu0.apic_id, (uint32)0);
  ASSERT(cpu0.is_online, true);
  ASSERT(cpu0.interrupt_stack_top > (size_t)g_interrupt_stacks[0], true);

  // Test secondary core state
  InitializeCpuCoreState(1, 4);
  CpuCoreState& cpu1 = g_cpu_cores[1];
  ASSERT(cpu1.self_pointer, &cpu1);
  ASSERT(cpu1.core_id, (uint32)1);
  ASSERT(cpu1.apic_id, (uint32)4);
  ASSERT(cpu1.is_online, false);
}

TEST(CpuCoreBitmasks) {
  g_online_cores_mask = 0;
  g_idle_cores_mask = 0;

  // Mark cores 0, 3, 63 online
  __atomic_fetch_or(&g_online_cores_mask, (1ULL << 0), __ATOMIC_RELAXED);
  __atomic_fetch_or(&g_online_cores_mask, (1ULL << 3), __ATOMIC_RELAXED);
  __atomic_fetch_or(&g_online_cores_mask, (1ULL << 63), __ATOMIC_RELAXED);

  ASSERT((g_online_cores_mask & (1ULL << 0)) != 0, true);
  ASSERT((g_online_cores_mask & (1ULL << 1)) != 0, false);
  ASSERT((g_online_cores_mask & (1ULL << 3)) != 0, true);
  ASSERT((g_online_cores_mask & (1ULL << 63)) != 0, true);

  // Mark cores 3 and 63 idle
  __atomic_fetch_or(&g_idle_cores_mask, (1ULL << 3), __ATOMIC_RELAXED);
  __atomic_fetch_or(&g_idle_cores_mask, (1ULL << 63), __ATOMIC_RELAXED);

  int first_idle = __builtin_ctzll(g_idle_cores_mask);
  ASSERT(first_idle, 3);

  // Clear core 3 from idle
  __atomic_fetch_and(&g_idle_cores_mask, ~(1ULL << 3), __ATOMIC_RELAXED);
  first_idle = __builtin_ctzll(g_idle_cores_mask);
  ASSERT(first_idle, 63);
}

TEST(TaskStateSegmentAndGdt) {
  InitializeCpuCoreState(0, 0);
  CpuCoreState& cpu0 = g_cpu_cores[0];

  ASSERT(sizeof(TaskStateSegment), static_cast<size_t>(104));
  ASSERT(cpu0.tss.rsp0, cpu0.interrupt_stack_top);
  ASSERT(cpu0.tss.iopb_offset, static_cast<uint16>(104));

  // GDT entry 1: Kernel code (64-bit, Ring 0)
  ASSERT(cpu0.gdt[kKernelCodeSelector / 8], 0x00209A0000000000ULL);
  // GDT entry 2: Kernel data (Ring 0)
  ASSERT(cpu0.gdt[kKernelDataSelector / 8], 0x0000920000000000ULL);
  // GDT entry 3: User data (Ring 3)
  ASSERT(cpu0.gdt[kUserDataSelector / 8], 0x0020F20000000000ULL);
  // GDT entry 4: User code (64-bit, Ring 3)
  ASSERT(cpu0.gdt[kUserCodeSelector / 8], 0x0020FA0000000000ULL);

  // Check TSS descriptor in GDT entries 5 and 6
  uint64 tss_low = cpu0.gdt[kTssGdtSelector / 8];
  uint64 tss_high = cpu0.gdt[(kTssGdtSelector / 8) + 1];
  ASSERT((tss_low & 0xFFFF), static_cast<uint64>(sizeof(TaskStateSegment) - 1));
  ASSERT((tss_low & (0x89ULL << 40)), (0x89ULL << 40));
  ASSERT(tss_high, (reinterpret_cast<size_t>(&cpu0.tss) >> 32) & 0xFFFFFFFFULL);
}
