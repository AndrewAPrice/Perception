// Copyright 2023 Google LLC
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

#include "syscalls.h"

const char *GetSystemCallName(int syscall) {
  return GetSystemCallName(static_cast<Syscall>(syscall));
}

const char *GetSystemCallName(Syscall syscall) {
  switch (syscall) {
    default:
      return "Unknown";
    case Syscall::PrintDebugCharacter:
      return "Syscall::PrintDebugCharacter";
    case Syscall::PrintRegistersAndStack:
      return "Syscall::PrintRegistersAndStack";
    case Syscall::CreateThread:
      return "Syscall::CreateThread";
    case Syscall::GetThisThreadId:
      return "Syscall::GetThisThreadId";
    case Syscall::SleepThisThread:
      return "Syscall::SleepThisThread";
    case Syscall::SleepThread:
      return "Syscall::SleepThread";
    case Syscall::WakeThread:
      return "Syscall::WakeThread";
    case Syscall::WaitAndSwitchToThread:
      return "Syscall::WaitAndSwitchToThread";
    case Syscall::TerminateThisThread:
      return "Syscall::TerminateThisThread";
    case Syscall::TerminateThread:
      return "Syscall::TerminateThread";
    case Syscall::Yield:
      return "Syscall::Yield";
    case Syscall::SetThreadSegment:
      return "Syscall::SetThreadSegment";
    case Syscall::SetAddressToClearOnThreadTermination:
      return "Syscall::SetAddressToClearOnThreadTermination";
    case Syscall::AllocateMemoryPages:
      return "Syscall::AllocateMemoryPages";
    case Syscall::AllocateMemoryPagesBelowPhysicalBase:
      return "Syscall::AllocateMemoryPagesBelowPhysicalBase";
    case Syscall::ReleaseMemoryPages:
      return "Syscall::ReleaseMemoryPages";
    case Syscall::MapPhysicalMemory:
      return "Syscall::MapPhysicalMemory";
    case Syscall::GetPhysicalAddressOfVirtualAddress:
      return "Syscall::GetPhysicalAddressOfVirtualAddress";
    case Syscall::GetFreeSystemMemory:
      return "Syscall::GetFreeSystemMemory";
    case Syscall::GetMemoryUsedByProcess:
      return "Syscall::GetMemoryUsedByProcess";
    case Syscall::GetTotalSystemMemory:
      return "Syscall::GetTotalSystemMemory";
    case Syscall::CreateSharedMemory:
      return "Syscall::CreateSharedMemory";
    case Syscall::JoinSharedMemory:
      return "Syscall::JoinSharedMemory";
    case Syscall::LeaveSharedMemory:
      return "Syscall::LeaveSharedMemory";
    case Syscall::MovePageIntoSharedMemory:
      return "Syscall::MovePageIntoSharedMemory";
    case Syscall::IsSharedMemoryPageAllocated:
      return "Syscall::IsSharedMemoryPageAllocated";
    case Syscall::SetMemoryAccessRights:
      return "Syscall::SetMemoryAccessRights";
    case Syscall::GetThisProcessId:
      return "Syscall::GetThisProcessId";
    case Syscall::TerminateThisProcess:
      return "Syscall::TerminateThisProcess";
    case Syscall::TerminateProcess:
      return "Syscall::TerminateProcess";
    case Syscall::GetProcesses:
      return "Syscall::GetProcesses";
    case Syscall::GetNameOfProcess:
      return "Syscall::GetNameOfProcess";
    case Syscall::NotifyWhenProcessDisappears:
      return "Syscall::NotifyWhenProcessDisappears";
    case Syscall::StopNotifyingWhenProcessDisappears:
      return "Syscall::StopNotifyingWhenProcessDisappears";
    case Syscall::CreateProcess:
      return "Syscall::CreateProcess";
    case Syscall::SetChildProcessMemoryPage:
      return "Syscall::SetChildProcessMemoryPage";
    case Syscall::StartExecutionProcess:
      return "Syscall::StartExecutionProcess";
    case Syscall::DestroyChildProcess:
      return "Syscall::DestroyChildProcess";
    case Syscall::RegisterService:
      return "Syscall::RegisterService";
    case Syscall::UnregisterService:
      return "Syscall::UnregisterService";
    case Syscall::GetServices:
      return "Syscall::GetServices";
    case Syscall::GetNameOfService:
      return "Syscall::GetNameOfService";
    case Syscall::NotifyWhenServiceAppears:
      return "Syscall::NotifyWhenServiceAppears";
    case Syscall::StopNotifyingWhenServiceAppears:
      return "Syscall::StopNotifyingWhenServiceAppears";
    case Syscall::NotifyWhenServiceDisappears:
      return "Syscall::NotifyWhenServiceDisappears";
    case Syscall::StopNotifyingWhenServiceDisappears:
      return "Syscall::StopNotifyingWhenServiceDisappears";
    case Syscall::SendMessage:
      return "Syscall::SendMessage";
    case Syscall::PollForMessage:
      return "Syscall::PollForMessage";
    case Syscall::SleepForMessage:
      return "Syscall::SleepForMessage";
    case Syscall::RegisterMessageToSendOnInterrupt:
      return "Syscall::RegisterMessageToSendOnInterrupt";
    case Syscall::UnregisterMessageToSendOnInterrupt:
      return "Syscall::UnregisterMessageToSendOnInterrupt";
    case Syscall::GetMultibootFramebufferInformation:
      return "Syscall::GetMultibootFramebufferInformation";
    case Syscall::SendMessageAfterXMicroseconds:
      return "Syscall::SendMessageAfterXMicroseconds";
    case Syscall::SendMessageAtTimestamp:
      return "Syscall::SendMessageAtTimestamp";
    case Syscall::GetCurrentTimestamp:
      return "Syscall::GetCurrentTimestamp";
  }
}
