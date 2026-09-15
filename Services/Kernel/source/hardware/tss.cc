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

#include "hardware/tss.h"

#include "memory/memory.h"

namespace hardware {

namespace {

// Available 64-bit TSS descriptor type (Present bit | Type 0x9).
constexpr uint64 kTssDescriptorType = 0x89ULL;

}  // namespace

void InitializeTaskStateSegment(TaskStateSegment& tss,
                                size_t interrupt_stack_top,
                                size_t double_fault_stack_top,
                                size_t nmi_stack_top) {
  memset(reinterpret_cast<char*>(&tss), 0, sizeof(TaskStateSegment));
  tss.rsp0 = interrupt_stack_top;
  tss.ist1 = double_fault_stack_top;
  tss.ist2 = nmi_stack_top;
  tss.iopb_offset = static_cast<uint16>(sizeof(TaskStateSegment));
}

void SetTssDescriptor(const TaskStateSegment& tss, uint64* gdt_entries) {
  size_t tss_base = reinterpret_cast<size_t>(&tss);
  size_t tss_limit = sizeof(TaskStateSegment) - 1;

  uint64 tss_low = (tss_limit & 0xFFFF) | ((tss_base & 0xFFFFFF) << 16) |
                   (kTssDescriptorType << 40) |
                   (((tss_limit >> 16) & 0xF) << 48) |
                   (((tss_base >> 24) & 0xFF) << 56);
  uint64 tss_high = (tss_base >> 32) & 0xFFFFFFFFULL;

  gdt_entries[0] = tss_low;
  gdt_entries[1] = tss_high;
}

void LoadTaskStateSegment(uint16 selector) {
#ifndef TEST
  asm volatile("ltr %0" ::"r"(selector));
#endif
}

}  // namespace hardware

