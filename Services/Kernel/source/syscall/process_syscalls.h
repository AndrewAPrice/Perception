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

// Handles Syscall::GetThisProcessId.
void GetThisProcessId(SyscallContext& context);

// Handles Syscall::TerminateThisProcess.
void TerminateThisProcess(SyscallContext& context);

// Handles Syscall::TerminateProcess.
void TerminateProcess(SyscallContext& context);

// Handles Syscall::GetProcesses.
void GetProcesses(SyscallContext& context);

// Handles Syscall::GetNameOfProcess.
void GetNameOfProcess(SyscallContext& context);

// Handles Syscall::NotifyWhenProcessDisappears.
void NotifyWhenProcessDisappears(SyscallContext& context);

// Handles Syscall::StopNotifyingWhenProcessDisappears.
void StopNotifyingWhenProcessDisappears(SyscallContext& context);

// Handles Syscall::CreateProcess.
void CreateProcess(SyscallContext& context);

// Handles Syscall::SetChildProcessMemoryPages.
void SetChildProcessMemoryPages(SyscallContext& context);

// Handles Syscall::StartExecutionProcess.
void StartExecutionProcess(SyscallContext& context);

// Handles Syscall::DestroyChildProcess.
void DestroyChildProcess(SyscallContext& context);

// Handles Syscall::SetFocusedProcess.
void SetFocusedProcess(SyscallContext& context);

}  // namespace syscall
