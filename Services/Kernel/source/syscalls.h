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

#pragma once

// The total number of system calls.
#define NUMBER_OF_SYSCALLS 62

// The canonical list of system calls, mapped to the system call number.
enum class Syscall {
  // Syscalls
  PrintDebugCharacter = 0,
  PrintRegistersAndStack = 26,
  // Threading,
  CreateThread = 1,
  GetThisThreadId = 2,
  SleepThisThread = 3,
  SleepThread = 9,
  WakeThread = 10,
  WaitAndSwitchToThread = 11,
  TerminateThisThread = 4,
  TerminateThread = 5,
  Yield = 8,
  SetThreadSegment = 27,
  SetAddressToClearOnThreadTermination = 28,
  // Memory management,
  AllocateMemoryPages = 12,
  AllocateMemoryPagesBelowPhysicalBase = 49,
  ReleaseMemoryPages = 13,
  MapPhysicalMemory = 41,
  GetPhysicalAddressOfVirtualAddress = 50,
  GetFreeSystemMemory = 14,
  GetMemoryUsedByProcess = 15,
  GetTotalSystemMemory = 16,
  CreateSharedMemory = 42,
  JoinSharedMemory = 43,
  JoinChildProcessInSharedMemory = 61,
  LeaveSharedMemory = 44,
  GetSharedMemoryDetails = 58,
  MovePageIntoSharedMemory = 45,
  GrantPermissionToAllocateIntoSharedMemory = 57,
  IsSharedMemoryPageAllocated = 46,
  GetSharedMemoryPagePhysicalAddress = 59,
  SetMemoryAccessRights = 48,
  // Processes,
  GetThisProcessId = 39,
  TerminateThisProcess = 6,
  TerminateProcess = 7,
  GetProcesses = 22,
  GetNameOfProcess = 29,
  NotifyWhenProcessDisappears = 30,
  StopNotifyingWhenProcessDisappears = 31,
  CreateProcess = 51,
  SetChildProcessMemoryPage = 52,
  StartExecutionProcess = 53,
  DestroyChildProcess = 54,
  GetMultibootModule = 60,
  // Services,
  RegisterService = 32,
  UnregisterService = 33,
  GetServices = 34,
  GetNameOfService = 47,
  NotifyWhenServiceAppears = 35,
  StopNotifyingWhenServiceAppears = 36,
  NotifyWhenServiceDisappears = 37,
  StopNotifyingWhenServiceDisappears = 38,
  // Messaging,
  SendMessage = 17,
  PollForMessage = 18,
  SleepForMessage = 19,
  // Interrupts,
  RegisterMessageToSendOnInterrupt = 20,
  UnregisterMessageToSendOnInterrupt = 21,
  // Drivers,
  GetMultibootFramebufferInformation = 40,
  // Time,
  SendMessageAfterXMicroseconds = 23,
  SendMessageAtTimestamp = 24,
  GetCurrentTimestamp = 25,
  // Profiling
  EnableProfiling = 55,
  DisableAndOutputProfiling = 56
};

// Returns the name of a system call, by int.
const char *GetSystemCallName(int syscall);

// Returns the name of a system call, by enum.
const char *GetSystemCallName(Syscall syscall);

// Actual handling of the system call execution is handled in SyscallHandler in
// syscall.cc.
