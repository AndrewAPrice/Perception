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
      return "PrintDebugCharacter";
    case Syscall::PrintRegistersAndStack:
      return "PrintRegistersAndStack";
    case Syscall::CreateThread:
      return "CreateThread";
    case Syscall::GetThisThreadId:
      return "GetThisThreadId";
    case Syscall::SleepThisThread:
      return "SleepThisThread";
    case Syscall::SleepThread:
      return "SleepThread";
    case Syscall::WakeThread:
      return "WakeThread";
    case Syscall::WaitAndSwitchToThread:
      return "WaitAndSwitchToThread";
    case Syscall::TerminateThisThread:
      return "TerminateThisThread";
    case Syscall::TerminateThread:
      return "TerminateThread";
    case Syscall::Yield:
      return "Yield";
    case Syscall::SetThreadSegment:
      return "SetThreadSegment";
    case Syscall::SetAddressToClearOnThreadTermination:
      return "SetAddressToClearOnThreadTermination";
    case Syscall::AllocateMemoryPages:
      return "AllocateMemoryPages";
    case Syscall::AllocateMemoryPagesBelowPhysicalBase:
      return "AllocateMemoryPagesBelowPhysicalBase";
    case Syscall::ReleaseMemoryPages:
      return "ReleaseMemoryPages";
    case Syscall::MapPhysicalMemory:
      return "MapPhysicalMemory";
    case Syscall::GetPhysicalAddressOfVirtualAddress:
      return "GetPhysicalAddressOfVirtualAddress";
    case Syscall::GetFreeSystemMemory:
      return "GetFreeSystemMemory";
    case Syscall::GetMemoryUsedByProcess:
      return "GetMemoryUsedByProcess";
    case Syscall::GetTotalSystemMemory:
      return "GetTotalSystemMemory";
    case Syscall::CreateSharedMemory:
      return "CreateSharedMemory";
    case Syscall::JoinSharedMemory:
      return "JoinSharedMemory";
    case Syscall::JoinChildProcessInSharedMemory:
      return "JoinChildProcessInSharedMemory";
    case Syscall::LeaveSharedMemory:
      return "LeaveSharedMemory";
    case Syscall::GetSharedMemoryDetails:
      return "GetSharedMemoryDetails";
    case Syscall::MovePageIntoSharedMemory:
      return "MovePageIntoSharedMemory";
    case Syscall::GrantPermissionToAllocateIntoSharedMemory:
      return "GrantPermissionToAllocateIntoSharedMemory";
    case Syscall::IsSharedMemoryPageAllocated:
      return "IsSharedMemoryPageAllocated";
    case Syscall::GetSharedMemoryPagePhysicalAddress:
      return "GetSharedMemoryPagePhysicalAddress";
    case Syscall::SetMemoryAccessRights:
      return "SetMemoryAccessRights";
    case Syscall::GetThisProcessId:
      return "GetThisProcessId";
    case Syscall::TerminateThisProcess:
      return "TerminateThisProcess";
    case Syscall::TerminateProcess:
      return "TerminateProcess";
    case Syscall::GetProcesses:
      return "GetProcesses";
    case Syscall::GetNameOfProcess:
      return "GetNameOfProcess";
    case Syscall::NotifyWhenProcessDisappears:
      return "NotifyWhenProcessDisappears";
    case Syscall::StopNotifyingWhenProcessDisappears:
      return "StopNotifyingWhenProcessDisappears";
    case Syscall::CreateProcess:
      return "CreateProcess";
    case Syscall::SetChildProcessMemoryPage:
      return "SetChildProcessMemoryPage";
    case Syscall::StartExecutionProcess:
      return "StartExecutionProcess";
    case Syscall::DestroyChildProcess:
      return "DestroyChildProcess";
    case Syscall::GetMultibootModule:
      return "GetMultibootModule";
    case Syscall::RegisterService:
      return "RegisterService";
    case Syscall::UnregisterService:
      return "UnregisterService";
    case Syscall::GetServices:
      return "GetServices";
    case Syscall::GetNameOfService:
      return "GetNameOfService";
    case Syscall::NotifyWhenServiceAppears:
      return "NotifyWhenServiceAppears";
    case Syscall::StopNotifyingWhenServiceAppears:
      return "StopNotifyingWhenServiceAppears";
    case Syscall::NotifyWhenServiceDisappears:
      return "NotifyWhenServiceDisappears";
    case Syscall::StopNotifyingWhenServiceDisappears:
      return "StopNotifyingWhenServiceDisappears";
    case Syscall::SendMessage:
      return "SendMessage";
    case Syscall::PollForMessage:
      return "PollForMessage";
    case Syscall::SleepForMessage:
      return "SleepForMessage";
    case Syscall::RegisterMessageToSendOnInterrupt:
      return "RegisterMessageToSendOnInterrupt";
    case Syscall::UnregisterMessageToSendOnInterrupt:
      return "UnregisterMessageToSendOnInterrupt";
    case Syscall::GetMultibootFramebufferInformation:
      return "GetMultibootFramebufferInformation";
    case Syscall::SendMessageAfterXMicroseconds:
      return "SendMessageAfterXMicroseconds";
    case Syscall::SendMessageAtTimestamp:
      return "SendMessageAtTimestamp";
    case Syscall::GetCurrentTimestamp:
      return "GetCurrentTimestamp";
  }
}
