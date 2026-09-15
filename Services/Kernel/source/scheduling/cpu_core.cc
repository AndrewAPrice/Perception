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

#include "hardware/io.h"
#include "memory/memory.h"

namespace scheduling {

using hardware::kGsBaseMsr;
using hardware::kKernelGsBaseMsr;
using hardware::LoadTaskStateSegment;
using hardware::WriteModelSpecificRegister;

namespace {

// Descriptor format reference for loading GDT with lgdt.
struct GdtDescriptor {
  uint16 limit;
  size_t base;
} __attribute__((packed));

static_assert(__builtin_offsetof(CpuCoreState, self_pointer) == 0,
              "self_pointer offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, syscall_scratch_rsp) == 8,
              "syscall_scratch_rsp offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState,
                                 currently_executing_thread_regs) == 16,
              "currently_executing_thread_regs offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, interrupt_stack_top) == 24,
              "interrupt_stack_top offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, running_thread) == 32,
              "running_thread offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, current_address_space) == 40,
              "current_address_space offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, core_id) == 48,
              "core_id offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, apic_id) == 52,
              "apic_id offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, is_online) == 56,
              "is_online offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, idle_regs) == 64,
              "idle_regs offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, tss) == 224,
              "tss offset mismatch");
static_assert(__builtin_offsetof(CpuCoreState, gdt) == 336,
              "gdt offset mismatch");

}  // namespace

// Global instances for all cores.
CpuCoreState g_cpu_cores[kMaxCores];
uint8 g_interrupt_stacks[kMaxCores][kInterruptStackSize] __attribute__((aligned(4096)));
uint8 g_double_fault_stacks[kMaxCores][kDoubleFaultStackSize] __attribute__((aligned(4096)));
uint8 g_nmi_stacks[kMaxCores][kNmiStackSize] __attribute__((aligned(4096)));
uint64 g_online_cores_mask = 0;
uint64 g_idle_cores_mask = 0;
size_t g_active_core_count = 1;

void InitializeCpuCoreState(size_t core_id, uint32 apic_id) {
  if (core_id >= static_cast<size_t>(kMaxCores)) return;

  CpuCoreState& cpu = g_cpu_cores[core_id];
  cpu.self_pointer = &cpu;
  cpu.syscall_scratch_rsp = 0;
  cpu.currently_executing_thread_regs = &cpu.idle_regs;
  cpu.interrupt_stack_top =
      reinterpret_cast<size_t>(&g_interrupt_stacks[core_id][kInterruptStackSize]);
  cpu.running_thread = nullptr;
  cpu.current_address_space = nullptr;
  cpu.cached_fs_base = static_cast<size_t>(-1);
  cpu.cached_user_gs_base = static_cast<size_t>(-1);
  cpu.core_id = static_cast<uint32>(core_id);
  cpu.apic_id = apic_id;
  cpu.is_online = (core_id == 0);

  size_t double_fault_stack_top =
      reinterpret_cast<size_t>(&g_double_fault_stacks[core_id][kDoubleFaultStackSize]);
  size_t nmi_stack_top =
      reinterpret_cast<size_t>(&g_nmi_stacks[core_id][kNmiStackSize]);

  // Initialize Task State Segment (TSS).
  InitializeTaskStateSegment(cpu.tss, cpu.interrupt_stack_top,
                             double_fault_stack_top, nmi_stack_top);

  // Initialize per-core Global Descriptor Table (GDT).
  cpu.gdt[0] = 0x0000000000000000ULL;  // Null descriptor
  cpu.gdt[kKernelCodeSelector / 8] =
      0x00209A0000000000ULL;  // Kernel code (64-bit, Ring 0)
  cpu.gdt[kKernelDataSelector / 8] =
      0x0000920000000000ULL;  // Kernel data (Ring 0)
  cpu.gdt[kUserDataSelector / 8] = 0x0020F20000000000ULL;  // User data (Ring 3)
  cpu.gdt[kUserCodeSelector / 8] =
      0x0020FA0000000000ULL;  // User code (64-bit, Ring 3)

  // 16-byte TSS descriptor in 64-bit mode spanning entries 5 and 6.
  SetTssDescriptor(cpu.tss, &cpu.gdt[kTssGdtSelector / 8]);
}

void LoadCpuCoreState(size_t core_id) {
#ifndef TEST
  if (core_id >= static_cast<size_t>(kMaxCores)) return;
  CpuCoreState& cpu = g_cpu_cores[core_id];

  // Invalidate cached segment bases.
  cpu.cached_fs_base = static_cast<size_t>(-1);
  cpu.cached_user_gs_base = static_cast<size_t>(-1);

  // Load per-core GDT.
  GdtDescriptor gdt_desc;
  gdt_desc.limit = static_cast<uint16>((kGdtEntryCount * sizeof(uint64)) - 1);
  gdt_desc.base = reinterpret_cast<size_t>(cpu.gdt);
  asm volatile("lgdt %0" ::"m"(gdt_desc));

  // Load per-core Task Register.
  LoadTaskStateSegment(kTssGdtSelector);

  // Set active and kernel GS base MSRs to point to this core's CpuCoreState.
  WriteModelSpecificRegister(kGsBaseMsr, reinterpret_cast<uint64>(&cpu));
  WriteModelSpecificRegister(kKernelGsBaseMsr, reinterpret_cast<uint64>(&cpu));
#endif
}

void InitializeCpuCores() {
  for (size_t i = 0; i < kMaxCores; i++) InitializeCpuCoreState(i, 0);

  g_online_cores_mask = 1ULL;
  g_idle_cores_mask = 0ULL;
  g_active_core_count = 1;

  LoadCpuCoreState(0);
}

}  // namespace scheduling
