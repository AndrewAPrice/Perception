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

// Handles Syscall::PrintDebugCharacter.
void PrintDebugCharacter(SyscallContext& context);

// Handles Syscall::PrintRegistersAndStack.
void PrintRegistersAndStack(SyscallContext& context);

// Handles Syscall::GetMultibootModule.
void GetMultibootModule(SyscallContext& context);

// Handles Syscall::GetMultibootFramebufferInformation.
void GetMultibootFramebufferInformation(SyscallContext& context);

// Handles Syscall::GetAcpiDetails.
void GetAcpiDetails(SyscallContext& context);

// Handles Syscall::RegisterMessageToSendOnInterrupt.
void RegisterMessageToSendOnInterrupt(SyscallContext& context);

// Handles Syscall::UnregisterMessageToSendOnInterrupt.
void UnregisterMessageToSendOnInterrupt(SyscallContext& context);

// Handles Syscall::SendMessageAfterXMicroseconds.
void SendMessageAfterXMicroseconds(SyscallContext& context);

// Handles Syscall::SendMessageAtTimestamp.
void SendMessageAtTimestamp(SyscallContext& context);

// Handles Syscall::GetCurrentTimestamp.
void GetCurrentTimestamp(SyscallContext& context);

// Handles Syscall::GetTimeInfo.
void GetTimeInfo(SyscallContext& context);

// Handles Syscall::SetTimeInfo.
void SetTimeInfo(SyscallContext& context);

// Handles Syscall::RegisterMessageForWhenTimeInfoChanges.
void RegisterMessageForWhenTimeInfoChanges(SyscallContext& context);

// Handles Syscall::EnableProfiling.
void EnableProfiling(SyscallContext& context);

// Handles Syscall::DisableAndOutputProfiling.
void DisableAndOutputProfiling(SyscallContext& context);

// Handles Syscall::SetThatProcessCaresAboutCpuTracking.
void SetThatProcessCaresAboutCpuTracking(SyscallContext& context);

}  // namespace syscall
