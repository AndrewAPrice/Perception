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

// Identifies the CPU core that is currently executing, without depending on the
// scheduling layer. Lives here, below both `containers/` and `scheduling/`, so
// that low-level utilities such as spinlocks and object pools can identify
// their core without creating a circular dependency on `scheduling/cpu_core.h`.
//
// The offsets below describe the leading fields of `scheduling::CpuCoreState`,
// which is reachable through the GS segment. They are asserted against the
// actual struct layout in `scheduling/cpu_core.cc`, alongside the assertions
// covering the offsets consumed by the assembly entry stubs.
namespace hardware {

// Maximum number of CPU cores supported by the kernel. Re-exported as
// `scheduling::kMaxCores`. Must not exceed 64, because TLB shootdown and
// online/idle core tracking all use a single uint64 as a bitmask of cores.
constexpr int kMaxCores = 16;

// Byte offset of the pointer each core holds to its own CpuCoreState.
constexpr size_t kCpuCoreSelfPointerOffset = 0;

// Byte offset of the logical core ID within CpuCoreState.
constexpr size_t kCpuCoreIdOffset = 48;

// Returns the logical core ID (0 to kMaxCores - 1) of the current physical
// core. Under TEST there is no GS base, so core 0 is always reported.
inline size_t GetCurrentCoreId() {
#ifndef TEST
  uint32 id;
  asm volatile("mov %%gs:%c1, %0" : "=r"(id) : "i"(kCpuCoreIdOffset));
  return static_cast<size_t>(id);
#else
  return 0;
#endif
}

}  // namespace hardware
