// Copyright 2020 Google LLC
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

// A thread's registers. The code in syscall.asm, exceptions.asm, and
// interrupts.asm depends on the layout of this struct.
struct Registers {
  uint64 r15, r14, r13, r12, r11, r10, r9, r8;
  uint64 rsi, rdx, rcx, rbx, rax, rdi, rbp;
  uint64 rip, cs, rflags, rsp, ss;
};

// Prints the registers, for debugging.
void PrintRegisters(const Registers& regs);

static_assert(sizeof(Registers) == 20 * 8, "Registers struct must be 160 bytes");
static_assert(__builtin_offsetof(Registers, r15) == 0 * 8);
static_assert(__builtin_offsetof(Registers, r14) == 1 * 8);
static_assert(__builtin_offsetof(Registers, r13) == 2 * 8);
static_assert(__builtin_offsetof(Registers, r12) == 3 * 8);
static_assert(__builtin_offsetof(Registers, r11) == 4 * 8);
static_assert(__builtin_offsetof(Registers, r10) == 5 * 8);
static_assert(__builtin_offsetof(Registers, r9) == 6 * 8);
static_assert(__builtin_offsetof(Registers, r8) == 7 * 8);
static_assert(__builtin_offsetof(Registers, rsi) == 8 * 8);
static_assert(__builtin_offsetof(Registers, rdx) == 9 * 8);
static_assert(__builtin_offsetof(Registers, rcx) == 10 * 8);
static_assert(__builtin_offsetof(Registers, rbx) == 11 * 8);
static_assert(__builtin_offsetof(Registers, rax) == 12 * 8);
static_assert(__builtin_offsetof(Registers, rdi) == 13 * 8);
static_assert(__builtin_offsetof(Registers, rbp) == 14 * 8);
static_assert(__builtin_offsetof(Registers, rip) == 15 * 8);
static_assert(__builtin_offsetof(Registers, cs) == 16 * 8);
static_assert(__builtin_offsetof(Registers, rflags) == 17 * 8);
static_assert(__builtin_offsetof(Registers, rsp) == 18 * 8);
static_assert(__builtin_offsetof(Registers, ss) == 19 * 8);

}  // namespace hardware