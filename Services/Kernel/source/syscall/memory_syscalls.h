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

#include "syscall/syscall_context.h"

namespace syscall {

// Handles Syscall::AllocateMemoryPages.
void AllocateMemoryPages(SyscallContext& context);

// Handles Syscall::AllocateMemoryPagesBelowPhysicalBase.
void AllocateMemoryPagesBelowPhysicalBase(SyscallContext& context);

// Handles Syscall::ReleaseMemoryPages.
void ReleaseMemoryPages(SyscallContext& context);

// Handles Syscall::MapPhysicalMemory.
void MapPhysicalMemory(SyscallContext& context);

// Handles Syscall::GetPhysicalAddressOfVirtualAddress.
void GetPhysicalAddressOfVirtualAddress(SyscallContext& context);

// Handles Syscall::GetSystemMetrics.
void GetSystemMetrics(SyscallContext& context);

// Handles Syscall::GetProcessHealthMetrics.
void GetProcessHealthMetrics(SyscallContext& context);

// Handles Syscall::SetMemoryAccessRights.
void SetMemoryAccessRights(SyscallContext& context);

}  // namespace syscall
