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
#include "hardware/registers.h"
#include "hardware/tss.h"
#include "types.h"

namespace memory {
class VirtualAddressSpace;
}

namespace scheduling {

struct Thread;

// Maximum number of CPU cores supported by the kernel. Defined in
// `hardware/current_core.h` so that the low level containers can size their
// per-core caches without depending on the scheduling layer.
using hardware::kMaxCores;

// Size in bytes of the dedicated kernel interrupt stack per core (16 KB).
constexpr size_t kInterruptStackSize = 16384;

// Size in bytes of the dedicated double fault IST stack per core (4 KB).
constexpr size_t kDoubleFaultStackSize = 4096;

// Size in bytes of the dedicated NMI IST stack per core (4 KB).
constexpr size_t kNmiStackSize = 4096;

// Number of temporary virtual memory mapping slots allocated per core.
constexpr int kTempSlotsPerCore = 8;

// Total number of temporary virtual memory mapping slots across all cores.
constexpr int kTotalTempSlots = kMaxCores * kTempSlotsPerCore;

// Number of 64-bit entries in each core's Global Descriptor Table.
constexpr size_t kGdtEntryCount = 7;

// Kernel code segment selector (Ring 0, 64-bit).
constexpr uint16 kKernelCodeSelector = 0x08;

// Kernel data segment selector (Ring 0).
constexpr uint16 kKernelDataSelector = 0x10;

// User data segment selector (Ring 3).
constexpr uint16 kUserDataSelector = 0x18;

// User code segment selector (Ring 3, 64-bit).
constexpr uint16 kUserCodeSelector = 0x20;

// Task State Segment selector in GDT.
constexpr uint16 kTssGdtSelector = 0x28;

// Request Privilege Level (RPL) for Ring 3 user mode.
constexpr uint16 kUserRpl = 3;

// State holding an isolated runtime context for each physical/logical CPU core.
// Layout of initial fields must match assembly offsets in syscall.asm and interrupts.asm.
struct CpuCoreState {
  // Offset 0: Pointer to this CpuCoreState instance (accessed via gs:0).
  CpuCoreState* self_pointer;

  // Offset 8: Scratch storage for user RSP during syscall entry (accessed via gs:8).
  size_t syscall_scratch_rsp;

  // Offset 16: Thread registers currently being executed on this core (accessed via gs:16).
  hardware::Registers* currently_executing_thread_regs;

  // Offset 24: Top of dedicated interrupt stack for this core (accessed via gs:24).
  size_t interrupt_stack_top;

  // Offset 32: Thread currently executing on this core (accessed via gs:32).
  Thread* running_thread;

  // Offset 40: VirtualAddressSpace currently loaded into this core's CR3 (accessed via gs:40).
  memory::VirtualAddressSpace* current_address_space;

  // Offset 48: Core index (0 to kMaxCores - 1).
  uint32 core_id;

  // Offset 52: Local APIC ID assigned by hardware.
  uint32 apic_id;

  // Offset 56: Flag indicating if this core has completed bootstrap and is active.
  bool is_online;

  // Registers to restore when idling (pointing to idle loop).
  hardware::Registers idle_regs __attribute__((aligned(16)));

  // Task State Segment for this core.
  hardware::TaskStateSegment tss __attribute__((aligned(16)));

  // Global Descriptor Table for this core.
  uint64 gdt[kGdtEntryCount] __attribute__((aligned(16)));

  // Currently loaded FS base MSR for this core.
  size_t cached_fs_base;

  // Currently loaded KernelGSBase MSR for this core.
  size_t cached_user_gs_base;

  // Cycle stamp of this core's most recent transition between user and kernel
  // space, used by the profiler.
  size_t profiling_transition_cycle;

  // Index into the profiler's event table of the kernel event this core is
  // currently executing.
  int profiling_event_index;

  // Query helpers for core state.
  uint32 id() const { return core_id; }
  uint32 apic() const { return apic_id; }
  bool online() const { return is_online; }
  bool is_idle() const { return running_thread == nullptr; }
  Thread* current_thread() const { return running_thread; }
  void set_running_thread(Thread* thread) { running_thread = thread; }
} __attribute__((aligned(64)));

// Global array of CpuCoreState structures for all supported cores.
extern CpuCoreState g_cpu_cores[kMaxCores];

// Per-core interrupt stacks.
extern uint8 g_interrupt_stacks[kMaxCores][kInterruptStackSize];

// Bitmask of cores that are booted and online.
extern uint64 g_online_cores_mask;

// Bitmask of cores currently halted in the scheduler idle loop.
extern uint64 g_idle_cores_mask;

// Number of active cores detected and online.
extern size_t g_active_core_count;

// Initializes the CpuCoreState structures for all supported cores, and loads
// BSP state.
void InitializeCpuCores();

// Initializes the CpuCoreState structure and hardware MSRs for the given core.
void InitializeCpuCoreState(size_t core_id, uint32 apic_id);

// Loads per-core GDT, TSS, and GS base MSRs on the current physical core.
void LoadCpuCoreState(size_t core_id);

// Returns a reference to the CpuCoreState of the current core.
inline CpuCoreState& GetCurrentCpuCore() {
#ifndef TEST
  CpuCoreState* ptr;
  asm volatile("mov %%gs:%c1, %0"
               : "=r"(ptr)
               : "i"(hardware::kCpuCoreSelfPointerOffset));
  return *ptr;
#else
  return g_cpu_cores[0];
#endif
}

// Re-exported so that scheduling code can identify its core without reaching
// into the hardware layer directly. The implementation lives in
// hardware/current_core.h so that containers/ can use it without depending on
// scheduling/.
using hardware::GetCurrentCoreId;

}  // namespace scheduling
