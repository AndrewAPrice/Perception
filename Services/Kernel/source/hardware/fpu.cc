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

#include "hardware/fpu.h"

#ifndef TEST
#include "hardware/io.h"
#include "common/kernel_string.h"
#include "memory/memory.h"
#include "memory/physical_allocator.h"
#include "containers/spinlock.h"
#include "output/text_terminal.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#else
#include <cstring>
#endif

namespace hardware {

#ifndef TEST

using containers::InterruptSafeSpinlock;
using containers::InterruptSafeSpinlockGuard;
using memory::KernelAddressSpace;
using memory::kPageSize;

namespace {

// CPUID Leaf 1 Register Bits
constexpr uint32 kCpuid1EcxBitXSave =
    1U << 26;  // XSAVE/XRSTOR instructions supported
constexpr uint32 kCpuid1EcxBitOsXSave = 1U << 27;  // CR4.OSXSAVE enabled by OS
constexpr uint32 kCpuid1EcxBitAvx =
    1U << 28;  // Advanced Vector Extensions (AVX) supported

// CPUID Leaf 13 (0xD) Subleaf 0 XCR0 Feature Bits
constexpr uint64 kXcr0BitX87 = 1ULL << 0;  // x87 FPU state (must always be 1)
constexpr uint64 kXcr0BitSse = 1ULL << 1;  // SSE state (XMM registers)
constexpr uint64 kXcr0BitAvx = 1ULL << 2;  // AVX state (YMM upper halves)
constexpr uint64 kXcr0BitAvx512Opmask =
    1ULL << 5;  // AVX-512 Opmask registers (k0-k7)
constexpr uint64 kXcr0BitAvx512ZmmHi256 =
    1ULL << 6;  // AVX-512 ZMM upper halves (ZMM0-ZMM15)
constexpr uint64 kXcr0BitAvx512Hi16Zmm = 1ULL
                                         << 7;  // AVX-512 ZMM16-ZMM31 registers

constexpr uint64 kXcr0Avx512Mask =
    kXcr0BitAvx512Opmask | kXcr0BitAvx512ZmmHi256 | kXcr0BitAvx512Hi16Zmm;

// Control Register 4 (CR4) Bits
constexpr uint64 kCr4BitOsXSave =
    1ULL << 18;  // Enable XSAVE and Processor Extended States

// FPU Save Area Pool Constants
constexpr size_t kFxSaveAreaSize = 512;

// CPUID Leaf 13 (0xD) Subleaf 1 Register Bits
constexpr uint32 kCpuid13Sub1EaxBitXSaveOpt = 1U << 0;

struct FpuSaveAreaPoolItem {
  FpuSaveAreaPoolItem* next;
};

// Spinlock protecting the global FPU save area free pool.
InterruptSafeSpinlock g_fpu_pool_lock;

FpuSaveAreaPoolItem* g_fpu_pool_head = nullptr;
size_t g_fpu_save_area_size = kFxSaveAreaSize;
bool g_xsave_supported = false;
bool g_xsaveopt_supported = false;
uint64 g_xcr0_mask = 0;

inline uint64 GetCr4() {
  uint64 cr4;
  asm volatile("mov %%cr4, %0" : "=r"(cr4));
  return cr4;
}

inline void SetCr4(uint64 cr4) { asm volatile("mov %0, %%cr4" : : "r"(cr4)); }

inline void SetXcr0(uint64 value) {
  uint32 low = value & 0xFFFFFFFF;
  uint32 high = value >> 32;
  asm volatile("xsetbv" : : "c"(0), "a"(low), "d"(high));
}

}  // namespace

void InitializeFpu() {
  uint32 eax = 0, ebx = 0, ecx = 0, edx = 0;
  GetCpuId(1, eax, ebx, ecx, edx);

  // Check if XSAVE is supported by hardware
  if ((ecx & kCpuid1EcxBitXSave) != 0) {
    SetCr4(GetCr4() | kCr4BitOsXSave);  // Enable CR4.OSXSAVE

    // Query supported XCR0 bits using CPUID leaf 0xD (13), subleaf 0
    GetCpuId(13, 0, eax, ebx, ecx, edx);
    uint64 supported_xcr0 = ((uint64)edx << 32) | eax;

    g_xcr0_mask = kXcr0BitX87 | kXcr0BitSse;
    if ((supported_xcr0 & kXcr0BitAvx) != 0) g_xcr0_mask |= kXcr0BitAvx;
    if ((supported_xcr0 & kXcr0Avx512Mask) == kXcr0Avx512Mask)
      g_xcr0_mask |= kXcr0Avx512Mask;

    SetXcr0(g_xcr0_mask);

    // Query required XSAVE area size for active XCR0 features (subleaf 0)
    GetCpuId(13, 0, eax, ebx, ecx, edx);
    g_fpu_save_area_size = ebx;
    g_xsave_supported = true;

    // Check if XSAVEOPT is supported (leaf 13, subleaf 1, bit 0).
    GetCpuId(13, 1, eax, ebx, ecx, edx);
    g_xsaveopt_supported = (eax & kCpuid13Sub1EaxBitXSaveOpt) != 0;
  } else {
    g_fpu_save_area_size = kFxSaveAreaSize;
    g_xsave_supported = false;
    g_xsaveopt_supported = false;
  }
}

void EnableFpuForCurrentCore() {
  if (g_xsave_supported) {
    SetCr4(GetCr4() | kCr4BitOsXSave);
    SetXcr0(g_xcr0_mask);
  }
}

FpuRegisters* AllocateFpuSaveArea() {
  {
    InterruptSafeSpinlockGuard guard(g_fpu_pool_lock);
    if (g_fpu_pool_head != nullptr) {
      FpuSaveAreaPoolItem* item = g_fpu_pool_head;
      g_fpu_pool_head = item->next;
      return reinterpret_cast<FpuRegisters*>(item);
    }
  }

  size_t pages_needed = (g_fpu_save_area_size + kPageSize - 1) / kPageSize;
  size_t virt_addr = KernelAddressSpace().AllocatePages(pages_needed);
  if (virt_addr == kOutOfMemory) return nullptr;

  memset(reinterpret_cast<char*>(virt_addr), 0, pages_needed * kPageSize);
  return reinterpret_cast<FpuRegisters*>(virt_addr);
}

void ReleaseFpuSaveArea(FpuRegisters* buffer) {
  if (!buffer) return;
  memset(reinterpret_cast<char*>(buffer), 0, g_fpu_save_area_size);
  InterruptSafeSpinlockGuard guard(g_fpu_pool_lock);
  FpuSaveAreaPoolItem* item = reinterpret_cast<FpuSaveAreaPoolItem*>(buffer);
  item->next = g_fpu_pool_head;
  g_fpu_pool_head = item;
}

void SaveFpuState(FpuRegisters* buffer) {
  if (!buffer) return;
  char* ptr = reinterpret_cast<char*>(buffer);
  if (g_xsaveopt_supported) {
    uint32 eax = 0xFFFFFFFF, edx = 0xFFFFFFFF;
    asm volatile("xsaveopt64 %0" : "=m"(*ptr) : "a"(eax), "d"(edx) : "memory");
  } else if (g_xsave_supported) {
    uint32 eax = 0xFFFFFFFF, edx = 0xFFFFFFFF;
    asm volatile("xsave64 %0" : "=m"(*ptr) : "a"(eax), "d"(edx) : "memory");
  } else {
    asm volatile("fxsave64 %0" : "=m"(*ptr) : : "memory");
  }
}

void RestoreFpuState(FpuRegisters* buffer) {
  if (!buffer) return;
  char* ptr = reinterpret_cast<char*>(buffer);
  if (g_xsave_supported) {
    uint32 eax = 0xFFFFFFFF, edx = 0xFFFFFFFF;
    asm volatile("xrstor64 %0" : : "m"(*ptr), "a"(eax), "d"(edx) : "memory");
  } else {
    asm volatile("fxrstor64 %0" : : "m"(*ptr) : "memory");
  }
}

#else  // TEST

void InitializeFpu() {}

void EnableFpuForCurrentCore() {}

FpuRegisters* AllocateFpuSaveArea() {
  auto* ptr = new char[512];
  memset(ptr, 0, 512);
  return reinterpret_cast<FpuRegisters*>(ptr);
}

void ReleaseFpuSaveArea(FpuRegisters* buffer) {
  delete[] reinterpret_cast<char*>(buffer);
}

void SaveFpuState(FpuRegisters* buffer) {}

void RestoreFpuState(FpuRegisters* buffer) {}

#endif  // TEST

}  // namespace hardware
