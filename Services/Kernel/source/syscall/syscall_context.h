// Copyright 2024 Google LLC
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

#include "hardware/registers.h"
#include "scheduling/thread.h"
#include "types.h"

namespace processes {
struct Process;
}

namespace syscall {

// Encapsulates the execution context of a system call, providing type-safe
// argument extraction and result writing on top of the saved registers.
class SyscallContext {
 public:
  SyscallContext(hardware::Registers& regs, scheduling::Thread* thread)
      : regs_(regs), thread_(thread) {}

  // Context accessors
  bool has_thread() const { return thread_ != nullptr; }
  scheduling::Thread* thread() const { return thread_; }
  scheduling::Thread& ThreadRef() const { return *thread_; }
  processes::Process* process() const {
    return thread_ != nullptr ? thread_->process : nullptr;
  }
  processes::Process& ProcessRef() const { return *thread_->process; }
  hardware::Registers& registers() { return regs_; }
  const hardware::Registers& registers() const { return regs_; }

  // Standard argument registers
  size_t arg0() const { return regs_.rax; }
  size_t arg1() const { return regs_.rbx; }
  size_t arg2() const { return regs_.rdx; }
  size_t arg3() const { return regs_.rsi; }
  size_t arg4() const { return regs_.r8; }
  size_t arg5() const { return regs_.r9; }
  size_t arg6() const { return regs_.r10; }

  // Special register accessors
  size_t arg_rdi() const { return regs_.rdi; }
  size_t arg_r12() const { return regs_.r12; }
  size_t arg_r13() const { return regs_.r13; }
  size_t arg_r14() const { return regs_.r14; }
  size_t arg_r15() const { return regs_.r15; }

  // Register mutators for non-standard returns
  void set_rdi(size_t val) { regs_.rdi = val; }
  void set_r12(size_t val) { regs_.r12 = val; }
  void set_r13(size_t val) { regs_.r13 = val; }
  void set_r14(size_t val) { regs_.r14 = val; }
  void set_r15(size_t val) { regs_.r15 = val; }

  // Return value setters
  void Return(size_t val) { regs_.rax = val; }
  void Return(int val) { regs_.rax = static_cast<size_t>(val); }
  void ReturnBoolean(bool val) { regs_.rax = val ? 1 : 0; }
  void ReturnSuccess() { regs_.rax = 0; }
  void ReturnError(size_t error_code = 0) { regs_.rax = error_code; }

  // Multi-register return setters
  void SetReturnValues(size_t r0, size_t r1) {
    regs_.rax = r0;
    regs_.rbx = r1;
  }

  void SetReturnValues(size_t r0, size_t r1, size_t r2) {
    regs_.rax = r0;
    regs_.rbx = r1;
    regs_.rdx = r2;
  }

  void SetReturnValues(size_t r0, size_t r1, size_t r2, size_t r3) {
    regs_.rax = r0;
    regs_.rbx = r1;
    regs_.rdx = r2;
    regs_.rsi = r3;
  }

  // Registers used, in order, to pass a block of 64-bit words in and out of a
  // syscall.
  static constexpr size_t kMaxWordsInRegisters = 10;

  // Extracts words from the input registers into the provided array. The array
  // is taken by reference so that its length is known, and no more words can be
  // extracted than it can hold.
  template <size_t kWords>
  void ExtractWords(size_t (&buffer)[kWords]) const {
    static_assert(kWords <= kMaxWordsInRegisters,
                  "Only kMaxWordsInRegisters words are passed in registers.");
    const uint64* const registers[kMaxWordsInRegisters] = {
        &regs_.rax, &regs_.rbx, &regs_.rdx, &regs_.rsi, &regs_.r8,
        &regs_.r9,  &regs_.r10, &regs_.r12, &regs_.r13, &regs_.r14};
    for (size_t i = 0; i < kWords; i++) buffer[i] = *registers[i];
  }

  // Writes words into the return registers. The array is taken by reference so
  // that its length is known, and no more words can be written than it holds.
  template <size_t kWords>
  void WriteWords(const size_t (&buffer)[kWords]) {
    static_assert(kWords <= kMaxWordsInRegisters,
                  "Only kMaxWordsInRegisters words are passed in registers.");
    uint64* const registers[kMaxWordsInRegisters] = {
        &regs_.rax, &regs_.rbx, &regs_.rdx, &regs_.rsi, &regs_.r8,
        &regs_.r9,  &regs_.r10, &regs_.r12, &regs_.r13, &regs_.r14};
    for (size_t i = 0; i < kWords; i++) *registers[i] = buffer[i];
  }

 private:
  hardware::Registers& regs_;
  scheduling::Thread* thread_;
};

}  // namespace syscall
