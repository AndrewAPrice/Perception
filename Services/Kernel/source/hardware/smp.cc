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

#include "hardware/smp.h"

#ifndef TEST
#include "boot/ap_boot.asm.h"
#include "boot/boot.asm.h"
#include "containers/spinlock.h"
#include "hardware/acpi.h"
#include "hardware/fpu.h"
#include "hardware/io.h"
#include "hardware/lapic.h"
#include "hardware/tlb_shootdown.h"
#include "interrupts/idt.h"
#include "interrupts/interrupts.asm.h"
#include "memory/memory.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#include "output/text_terminal.h"
#include "scheduling/cpu_core.h"
#include "scheduling/scheduler.h"
#include "scheduling/timer.h"
#include "syscall/syscall.h"
#endif

extern "C" size_t Pml4;

namespace hardware {

#ifndef TEST
using containers::InterruptSafeSpinlockGuard;
using memory::KernelAddressSpace;
using memory::TemporarilyMapPhysicalPages;
using output::NumberFormat;
using output::print;
using scheduling::g_active_core_count;
using scheduling::g_cpu_cores;
using scheduling::g_idle_cores_mask;
using scheduling::g_lapic_ticks_per_microsecond;
using scheduling::g_online_cores_mask;
using scheduling::GetCurrentCoreId;
using scheduling::kMaxCores;
using scheduling::LoadCpuCoreState;
using scheduling::SleepMicroseconds;
#endif

namespace {

// Base physical address where the real-mode AP trampoline is loaded.
constexpr size_t kTrampolineBasePhys = 0x8000;

// Vector number passed in the Startup IPI (0x8000 >> 12).
constexpr uint8 kStartupIpiVector = 0x08;

// Byte offset of the PML4 physical address within the trampoline.
constexpr size_t kTrampolinePml4Offset = 0x04;

// Byte offset of the stack top pointer within the trampoline.
constexpr size_t kTrampolineStackOffset = 0x08;

// Byte offset of the logical core ID within the trampoline.
constexpr size_t kTrampolineCoreIdOffset = 0x10;

// Byte offset of the status flag within the trampoline.
constexpr size_t kTrampolineStatusOffset = 0x14;

// Delivery mode for INIT IPI.
constexpr uint32 kIcrModeInit = 0x00000500;

// Delivery mode for Startup IPI (SIPI).
constexpr uint32 kIcrModeStartup = 0x00000600;

// Delivery mode for Fixed IPI.
constexpr uint32 kIcrModeFixed = 0x00000000;

// Level Assert bit in ICR.
constexpr uint32 kIcrLevelAssert = 0x00004000;

// Destination shorthand: All excluding self.
constexpr uint32 kIcrShorthandAllExcludingSelf = 0x000C0000;

// Delivery status bit (bit 12: 1 = send pending).
constexpr uint32 kIcrDeliveryStatusBusy = 1 << 12;

// Maximum iterations to poll for an AP to come online.
constexpr int kApOnlineTimeoutIterations = 1000;

// Delay after INIT IPI in microseconds (10 ms).
constexpr size_t kInitIpiDelayMicroseconds = 10000;

// Delay after first SIPI in microseconds (200 us).
constexpr size_t kStartupIpiDelayMicroseconds = 200;

// Poll interval while waiting for an AP to report online in microseconds (10
// us).
constexpr size_t kApPollIntervalMicroseconds = 10;

#ifndef TEST
// Serializes TLB shootdowns so only one is in flight at a time.
containers::InterruptSafeSpinlock g_shootdown_lock;

// Sends an Inter-Processor Interrupt (IPI) with given ICR register contents.
void SendIpiRaw(uint32 high, uint32 low) {
  while (ReadLapicRegister(kLapicIcrLowRegister) & kIcrDeliveryStatusBusy)
    __builtin_ia32_pause();

  WriteLapicRegister(kLapicIcrHighRegister, high);
  WriteLapicRegister(kLapicIcrLowRegister, low);
}
#endif

}  // namespace

void InitializeSmp() {
#ifndef TEST
  size_t core_count = GetDiscoveredCoreCount();
  if (core_count <= 1) {
    print << "SMP: Single core system detected.\n";
    return;
  }

  print << "SMP: Booting " << core_count << " cores...\n";

  // Map physical page 0x8000 to install the AP real-mode trampoline.
  void* trampoline_virt = TemporarilyMapPhysicalPages(kTrampolineBasePhys, 0);
  if (trampoline_virt == nullptr) {
    print << "SMP: Failed to map trampoline physical memory.\n";
    return;
  }

  size_t trampoline_size = reinterpret_cast<size_t>(ap_trampoline_end) -
                           reinterpret_cast<size_t>(ap_trampoline_start);
  if (trampoline_size > memory::kPageSize) {
    print << "SMP: Trampoline size exceeds page size.\n";
    return;
  }

  memcpy(reinterpret_cast<char*>(trampoline_virt),
         reinterpret_cast<const char*>(ap_trampoline_start), trampoline_size);

  // Set PML4 address in trampoline.
  *reinterpret_cast<uint32*>(reinterpret_cast<char*>(trampoline_virt) +
                             kTrampolinePml4Offset) =
      static_cast<uint32>(reinterpret_cast<size_t>(&Pml4));

  uint32 bsp_apic_id = ReadLapicRegister(kLapicIdRegister) >> 24;
  g_cpu_cores[0].apic_id = bsp_apic_id;

  size_t logical_core_id = 1;
  for (size_t i = 0; i < core_count && logical_core_id < static_cast<size_t>(kMaxCores); i++) {
    uint32 apic_id = GetCoreApicId(i);
    if (apic_id == bsp_apic_id) continue;

    size_t core_id = logical_core_id;
    g_cpu_cores[core_id].apic_id = apic_id;

    // Populate AP initial stack and core_id in trampoline.
    *reinterpret_cast<uint64*>(reinterpret_cast<char*>(trampoline_virt) +
                               kTrampolineStackOffset) =
        g_cpu_cores[core_id].interrupt_stack_top;
    *reinterpret_cast<uint32*>(reinterpret_cast<char*>(trampoline_virt) +
                               kTrampolineCoreIdOffset) =
        static_cast<uint32>(core_id);
    *reinterpret_cast<volatile uint32*>(reinterpret_cast<char*>(trampoline_virt) +
                                        kTrampolineStatusOffset) = 0;

    // Send INIT IPI.
    SendIpiRaw(apic_id << 24, kIcrLevelAssert | kIcrModeInit);
    SleepMicroseconds(kInitIpiDelayMicroseconds);

    // Send first Startup IPI (SIPI).
    SendIpiRaw(apic_id << 24, kIcrModeStartup | kStartupIpiVector);
    SleepMicroseconds(kStartupIpiDelayMicroseconds);

    // Send second Startup IPI (SIPI).
    SendIpiRaw(apic_id << 24, kIcrModeStartup | kStartupIpiVector);

    // Wait for the AP to report online.
    int timeout = kApOnlineTimeoutIterations;
    while (!__atomic_load_n(&g_cpu_cores[core_id].is_online, __ATOMIC_ACQUIRE) &&
           timeout > 0) {
      SleepMicroseconds(kApPollIntervalMicroseconds);
      timeout--;
    }

    if (__atomic_load_n(&g_cpu_cores[core_id].is_online, __ATOMIC_ACQUIRE)) {
      __atomic_fetch_add(&g_active_core_count, 1, __ATOMIC_SEQ_CST);
    } else {
      uint32 status = *reinterpret_cast<volatile uint32*>(
          reinterpret_cast<char*>(trampoline_virt) + kTrampolineStatusOffset);
      print << "SMP: Warning: Core "
            << output::NumberFormat::DecimalWithoutCommas << core_id
            << " (APIC " << static_cast<size_t>(apic_id)
            << ") failed to start. Status: " << static_cast<size_t>(status)
            << "\n";
    }
    logical_core_id++;
  }

  print << "SMP: " << output::NumberFormat::DecimalWithoutCommas
        << g_active_core_count << " cores online.\n";
#endif
}

extern "C" void ApMain(size_t core_id) {
#ifndef TEST
  LoadCpuCoreState(core_id);
  KernelAddressSpace().SwitchToAddressSpace();
  interrupts::LoadIdt();
  syscall::InitializeSystemCalls();
  EnableFpuForCurrentCore();

  // Now CR3 is loaded with KernelAddressSpace, so g_lapic_base is mapped and accessible.
  uint32 apic_id = ReadLapicRegister(kLapicIdRegister) >> 24;
  g_cpu_cores[core_id].apic_id = apic_id;

  // Enable Local APIC on this core.
  WriteLapicRegister(kLapicSpuriousInterruptVectorRegister, 0xFF | (1 << 8));
  WriteLapicRegister(kLapicTimerDivideConfigurationRegister, 3);

  // Mark this core online.
  __atomic_store_n(&g_cpu_cores[core_id].is_online, true, __ATOMIC_RELEASE);
  __atomic_fetch_or(&g_online_cores_mask, 1ULL << core_id, __ATOMIC_SEQ_CST);

  // Periodic LAPIC timer on reschedule vector every 10ms
  WriteLapicRegister(kLapicTimerLvtRegister,
                     kLapicTimerPeriodic | kRescheduleIpiInterruptVector);
  WriteLapicRegister(
      kLapicTimerInitialCountRegister,
      static_cast<uint32>(10000 * g_lapic_ticks_per_microsecond));

  // Register as idle before jumping into thread.
  scheduling::ScheduleNextThread();
  JumpIntoThread();
#endif
}

void SendRescheduleIpi(size_t target_core_id) {
#ifndef TEST
  if (target_core_id >= kMaxCores || !g_cpu_cores[target_core_id].is_online)
    return;
  uint32 apic_id = g_cpu_cores[target_core_id].apic_id;
  SendIpiRaw(apic_id << 24,
             kIcrLevelAssert | kIcrModeFixed | kRescheduleIpiInterruptVector);
#endif
}

void SendRescheduleIpiToAnyIdleCore() {
#ifndef TEST
  uint64 idle_mask = __atomic_load_n(&g_idle_cores_mask, __ATOMIC_ACQUIRE);
  size_t current_core = GetCurrentCoreId();
  idle_mask &= ~(1ULL << current_core);
  if (idle_mask != 0) {
    int target_core = __builtin_ctzll(idle_mask);
    SendRescheduleIpi(target_core);
  }
#endif
}

void BroadcastTlbShootdown(size_t address) {
#ifndef TEST
  if (__atomic_load_n(&g_active_core_count, __ATOMIC_ACQUIRE) <= 1) return;

  // Serialized so only one shootdown is in flight, because the address and the
  // pending mask are a single global pair.
  InterruptSafeSpinlockGuard guard(g_shootdown_lock);

  uint64 targets = __atomic_load_n(&g_online_cores_mask, __ATOMIC_ACQUIRE) &
                   ~(1ULL << GetCurrentCoreId());
  if (targets == 0) return;

  __atomic_store_n(&g_shootdown_address, address, __ATOMIC_RELEASE);
  __atomic_store_n(&g_shootdown_pending_mask, targets, __ATOMIC_RELEASE);

  SendIpiRaw(0, kIcrShorthandAllExcludingSelf | kIcrLevelAssert |
                    kIcrModeFixed | kTlbShootdownIpiInterruptVector);

  // Cores running in user space acknowledge via the IPI. Cores running kernel
  // code have interrupts disabled and instead acknowledge from whichever spin
  // loop they are in, so this wait cannot deadlock against a core blocked on a
  // lock this core holds.
  while (__atomic_load_n(&g_shootdown_pending_mask, __ATOMIC_ACQUIRE) != 0)
    containers::CpuPause();
#endif
}

}  // namespace hardware
