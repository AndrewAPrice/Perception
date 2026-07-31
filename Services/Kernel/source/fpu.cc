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

#include "fpu.h"

#ifndef TEST

#include "io.h"
#include "kernel_string.h"
#include "memory.h"
#include "physical_allocator.h"
#include "text_terminal.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

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

struct FpuSaveAreaPoolItem {
  FpuSaveAreaPoolItem* next;
};

FpuSaveAreaPoolItem* g_fpu_pool_head = nullptr;
size_t g_fpu_save_area_size = kFxSaveAreaSize;
bool g_xsave_supported = false;
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
  GetCpuId(1, &eax, &ebx, &ecx, &edx);

  // Check if XSAVE is supported by hardware
  if ((ecx & kCpuid1EcxBitXSave) != 0) {
    SetCr4(GetCr4() | kCr4BitOsXSave);  // Enable CR4.OSXSAVE

    // Query supported XCR0 bits using CPUID leaf 0xD (13), subleaf 0
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(13), "c"(0));
    uint64 supported_xcr0 = ((uint64)edx << 32) | eax;

    g_xcr0_mask = kXcr0BitX87 | kXcr0BitSse;
    if ((supported_xcr0 & kXcr0BitAvx) != 0) g_xcr0_mask |= kXcr0BitAvx;
    if ((supported_xcr0 & kXcr0Avx512Mask) == kXcr0Avx512Mask)
      g_xcr0_mask |= kXcr0Avx512Mask;

    SetXcr0(g_xcr0_mask);

    // Query required XSAVE area size for active XCR0 features (subleaf 0)
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(13), "c"(0));
    g_fpu_save_area_size = ebx;
    g_xsave_supported = true;
  } else {
    g_fpu_save_area_size = kFxSaveAreaSize;
    g_xsave_supported = false;
  }
}

FpuRegisters* AllocateFpuSaveArea() {
  if (g_fpu_pool_head != nullptr) {
    FpuSaveAreaPoolItem* item = g_fpu_pool_head;
    g_fpu_pool_head = item->next;
    return reinterpret_cast<FpuRegisters*>(item);
  }

  size_t pages_needed = (g_fpu_save_area_size + PAGE_SIZE - 1) / PAGE_SIZE;
  size_t virt_addr = KernelAddressSpace().AllocatePages(pages_needed);
  if (virt_addr == OUT_OF_MEMORY) return nullptr;

  return reinterpret_cast<FpuRegisters*>(virt_addr);
}

void ReleaseFpuSaveArea(FpuRegisters* buffer) {
  if (!buffer) return;
  FpuSaveAreaPoolItem* item = reinterpret_cast<FpuSaveAreaPoolItem*>(buffer);
  item->next = g_fpu_pool_head;
  g_fpu_pool_head = item;
}

void SaveFpuState(FpuRegisters* buffer) {
  if (!buffer) return;
  char* ptr = reinterpret_cast<char*>(buffer);
  if (g_xsave_supported) {
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

FpuRegisters* AllocateFpuSaveArea() {
  return reinterpret_cast<FpuRegisters*>(new char[512]);
}

void ReleaseFpuSaveArea(FpuRegisters* buffer) {
  delete[] reinterpret_cast<char*>(buffer);
}

void SaveFpuState(FpuRegisters* buffer) {}

void RestoreFpuState(FpuRegisters* buffer) {}

#endif  // TEST
