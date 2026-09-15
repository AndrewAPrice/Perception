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

// Layout of the 64-bit x86 Task State Segment (TSS).
struct TaskStateSegment {
  uint32 reserved0;
  uint64 rsp0;
  uint64 rsp1;
  uint64 rsp2;
  uint64 reserved1;
  uint64 ist1;
  uint64 ist2;
  uint64 ist3;
  uint64 ist4;
  uint64 ist5;
  uint64 ist6;
  uint64 ist7;
  uint64 reserved2;
  uint16 reserved3;
  uint16 iopb_offset;
} __attribute__((packed));

static_assert(sizeof(TaskStateSegment) == 104, "x86_64 TSS must be 104 bytes");

// Size in bytes of the 64-bit x86 Task State Segment.
constexpr size_t kTssSize = sizeof(TaskStateSegment);

// Initializes a Task State Segment with the given Ring 0 interrupt stack
// pointer and optional dedicated IST stacks for double fault and NMI.
void InitializeTaskStateSegment(TaskStateSegment& tss,
                                size_t interrupt_stack_top,
                                size_t double_fault_stack_top = 0,
                                size_t nmi_stack_top = 0);

// Encodes a 16-byte TSS descriptor for this TSS into two consecutive 64-bit GDT
// entries.
void SetTssDescriptor(const TaskStateSegment& tss, uint64* gdt_entries);

// Loads the Task Register (TR) with the given GDT segment selector.
void LoadTaskStateSegment(uint16 selector);

}  // namespace hardware