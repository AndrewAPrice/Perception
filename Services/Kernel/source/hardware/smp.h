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

// Discovers and bootstraps secondary CPU cores (APs).
void InitializeSmp();

// Sends a reschedule IPI to a specific CPU core.
void SendRescheduleIpi(size_t target_core_id);

// Sends a reschedule IPI to any currently idle CPU core, if one exists.
void SendRescheduleIpiToAnyIdleCore();

// Invalidates `address` in the TLB of every online CPU core in
// `target_cores_mask` (excluding the calling core), blocking until they have
// all acknowledged. Pass hardware::kFlushEntireTlb to invalidate their entire
// TLBs including global pages, or hardware::kFlushUserTlb to reload CR3.
void BroadcastTlbShootdown(size_t address,
                           uint64 target_cores_mask = ~static_cast<uint64>(0));

}  // namespace hardware


