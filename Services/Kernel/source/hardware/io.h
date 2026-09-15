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

// Reads a byte from a port.
uint8 ReadIOByte(unsigned short port);

// Writes a byte to a port.
void WriteIOByte(unsigned short port, unsigned char data);

// Reads a 16-bit word from a port.
uint16 ReadIO16Bits(unsigned short port);

// Writes a 16-bit word to a port.
void WriteIO16Bits(unsigned short port, unsigned short data);

// Model Specific Register containing the kernel SYSCALL entry point.
constexpr uint32 kLstarMsr = 0xC0000082;

// Model Specific Register containing CS/SS segment selectors for
// SYSCALL/SYSRET.
constexpr uint32 kStarMsr = 0xC0000081;

// Model Specific Register containing the RFLAGS mask during system calls.
constexpr uint32 kIa32FmaskMsr = 0xC0000084;

// Model Specific Register storing the FS segment base address.
constexpr uint32 kFsBaseMsr = 0xC0000100;

// Model Specific Register storing the active GS segment base address.
constexpr uint32 kGsBaseMsr = 0xC0000101;

// Model Specific Register storing the swapped kernel GS segment base address
// (swapgs).
constexpr uint32 kKernelGsBaseMsr = 0xC0000102;

// Sets a model-specific register.
void WriteModelSpecificRegister(uint64 msr, uint64 value);

// Reads a model-specific register.
uint64 ReadModelSpecificRegister(uint64 msr);

// Reads the CPU Time Stamp Counter (TSC).
uint64 ReadTimestampCounter();

// Executes the CPUID instruction.
void GetCpuId(uint32 leaf, uint32& eax, uint32& ebx, uint32& ecx, uint32& edx);

// Executes the CPUID instruction with a subleaf.
void GetCpuId(uint32 leaf, uint32 subleaf, uint32& eax, uint32& ebx,
              uint32& ecx, uint32& edx);

}  // namespace hardware


