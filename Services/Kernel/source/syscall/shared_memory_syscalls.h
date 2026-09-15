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

// Handles Syscall::CreateSharedMemory.
void CreateSharedMemory(SyscallContext& context);

// Handles Syscall::JoinSharedMemory.
void JoinSharedMemory(SyscallContext& context);

// Handles Syscall::JoinChildProcessInSharedMemory.
void JoinChildProcessInSharedMemory(SyscallContext& context);

// Handles Syscall::LeaveSharedMemory.
void LeaveSharedMemory(SyscallContext& context);

// Handles Syscall::GetSharedMemoryDetails.
void GetSharedMemoryDetails(SyscallContext& context);

// Handles Syscall::MovePageIntoSharedMemory.
void MovePageIntoSharedMemory(SyscallContext& context);

// Handles Syscall::GrantPermissionToAllocateIntoSharedMemory.
void GrantPermissionToAllocateIntoSharedMemory(SyscallContext& context);

// Handles Syscall::IsSharedMemoryPageAllocated.
void IsSharedMemoryPageAllocated(SyscallContext& context);

// Handles Syscall::GetSharedMemoryPagePhysicalAddress.
void GetSharedMemoryPagePhysicalAddress(SyscallContext& context);

// Handles Syscall::GrowSharedMemory.
void GrowSharedMemory(SyscallContext& context);

// Handles Syscall::RegisterSharedMemoryEvent.
void RegisterSharedMemoryEvent(SyscallContext& context);

// Handles Syscall::UnregisterSharedMemoryEvent.
void UnregisterSharedMemoryEvent(SyscallContext& context);

// Handles Syscall::TriggerSharedMemoryEvent.
void TriggerSharedMemoryEvent(SyscallContext& context);

}  // namespace syscall
