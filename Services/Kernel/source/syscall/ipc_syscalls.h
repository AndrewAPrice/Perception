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

// Handles Syscall::SendMessage.
void SendMessage(SyscallContext& context);

// Handles Syscall::PollForMessage.
void PollForMessage(SyscallContext& context);

// Handles Syscall::SleepForMessage.
void SleepForMessage(SyscallContext& context);

// Handles Syscall::SetSystemMessageHandlers.
void SetSystemMessageHandlers(SyscallContext& context);

// Handles Syscall::RegisterService.
void RegisterService(SyscallContext& context);

// Handles Syscall::UnregisterService.
void UnregisterService(SyscallContext& context);

// Handles Syscall::GetServices.
void GetServices(SyscallContext& context);

// Handles Syscall::GetNameOfService.
void GetNameOfService(SyscallContext& context);

// Handles Syscall::NotifyWhenServiceAppears.
void NotifyWhenServiceAppears(SyscallContext& context);

// Handles Syscall::StopNotifyingWhenServiceAppears.
void StopNotifyingWhenServiceAppears(SyscallContext& context);

// Handles Syscall::NotifyWhenServiceDisappears.
void NotifyWhenServiceDisappears(SyscallContext& context);

// Handles Syscall::StopNotifyingWhenServiceDisappears.
void StopNotifyingWhenServiceDisappears(SyscallContext& context);

}  // namespace syscall
