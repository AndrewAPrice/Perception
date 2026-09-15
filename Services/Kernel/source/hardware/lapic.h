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

#include "types.h"

namespace hardware {

// Local APIC ID register offset.
constexpr uint32 kLapicIdRegister = 0x20;

// Local APIC End Of Interrupt register offset.
constexpr uint32 kLapicEoiRegister = 0xB0;

// Local APIC Spurious Interrupt Vector Register offset.
constexpr uint32 kLapicSpuriousInterruptVectorRegister = 0xF0;

// Local APIC Interrupt Command Register (low 32 bits) offset.
constexpr uint32 kLapicIcrLowRegister = 0x300;

// Local APIC Interrupt Command Register (high 32 bits) offset.
constexpr uint32 kLapicIcrHighRegister = 0x310;

// Local APIC Timer LVT register offset.
constexpr uint32 kLapicTimerLvtRegister = 0x320;

// Local APIC Timer Initial Count register offset.
constexpr uint32 kLapicTimerInitialCountRegister = 0x380;

// Local APIC Timer Current Count register offset.
constexpr uint32 kLapicTimerCurrentCountRegister = 0x390;

// Local APIC Timer Divide Configuration register offset.
constexpr uint32 kLapicTimerDivideConfigurationRegister = 0x3E0;

// Local APIC Timer LVT register flag for masked (disabled) interrupts.
constexpr uint32 kLapicTimerMasked = 1 << 16;

// Local APIC Timer LVT register flag for periodic timer mode.
constexpr uint32 kLapicTimerPeriodic = 1 << 17;

// Interrupt vector number assigned for the Local APIC timer.
constexpr uint8 kApicTimerInterruptVector = 48;

// Interrupt vector number assigned for cross-core thread rescheduling.
constexpr uint8 kRescheduleIpiInterruptVector = 49;

// Interrupt vector number assigned for cross-core TLB invalidation.
constexpr uint8 kTlbShootdownIpiInterruptVector = 50;

// Global pointer to the memory-mapped Local APIC register space.
extern volatile uint32* g_lapic_base;

// Writes a 32-bit value to a Local APIC register.
inline void WriteLapicRegister(uint32 offset, uint32 value) {
#ifndef TEST
  if (g_lapic_base != nullptr) g_lapic_base[offset / 4] = value;
#endif
}

// Reads a 32-bit value from a Local APIC register.
inline uint32 ReadLapicRegister(uint32 offset) {
#ifndef TEST
  if (g_lapic_base != nullptr) return g_lapic_base[offset / 4];
#endif
  return 0;
}

// Sends an End Of Interrupt signal to the Local APIC.
inline void SendLapicEoi() {
#ifndef TEST
  if (g_lapic_base != nullptr) WriteLapicRegister(kLapicEoiRegister, 0);
#endif
}

}  // namespace hardware


