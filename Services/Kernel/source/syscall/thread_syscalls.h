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

// Handles Syscall::CreateThread.
void CreateThread(SyscallContext& context);

// Handles Syscall::GetThisThreadId.
void GetThisThreadId(SyscallContext& context);

// Handles Syscall::SleepThisThread.
void SleepThisThread(SyscallContext& context);

// Handles Syscall::SleepThread.
void SleepThread(SyscallContext& context);

// Handles Syscall::WakeThread.
void WakeThread(SyscallContext& context);

// Handles Syscall::TerminateThisThread.
void TerminateThisThread(SyscallContext& context);

// Handles Syscall::TerminateThread.
void TerminateThread(SyscallContext& context);

// Handles Syscall::SetThreadSegment.
void SetThreadSegment(SyscallContext& context);

// Handles Syscall::SetThreadSegmentExtended.
void SetThreadSegmentExtended(SyscallContext& context);

// Handles Syscall::SetAddressToClearOnThreadTermination.
void SetAddressToClearOnThreadTermination(SyscallContext& context);

// Handles Syscall::SetThreadPriority.
void SetThreadPriority(SyscallContext& context);

}  // namespace syscall
