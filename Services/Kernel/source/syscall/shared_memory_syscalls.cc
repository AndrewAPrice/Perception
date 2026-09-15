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

#ifndef TEST
#include "syscall/shared_memory_syscalls.h"

#include "ipc/shared_memory.h"
#include "memory/physical_allocator.h"
#include "processes/process.h"
#include "scheduling/thread.h"

using ipc::CreateAndMapSharedMemoryBlockIntoProcess;
using ipc::GetPhysicalAddressOfPageInSharedMemory;
using ipc::GetSharedMemoryDetailsPertainingToProcess;
using ipc::GrantPermissionToAllocateIntoSharedMemory;
using ipc::GrowSharedMemory;
using ipc::IsAddressAllocatedInSharedMemory;
using ipc::JoinChildProcessInSharedMemory;
using ipc::JoinSharedMemory;
using ipc::LeaveSharedMemory;
using ipc::MovePageIntoSharedMemory;
using ipc::RegisterSharedMemoryEvent;
using ipc::SharedMemoryInProcess;
using ipc::TriggerSharedMemoryEvent;
using ipc::UnregisterSharedMemoryEvent;
using processes::GetProcessFromPid;
using processes::Process;
using processes::ProcessRef;

namespace syscall {

void CreateSharedMemory(SyscallContext& context) {
  if (!context.has_thread()) return;

  SharedMemoryInProcess* shared_memory =
      CreateAndMapSharedMemoryBlockIntoProcess(
          context.process(), context.arg0(), context.arg1(), context.arg2());
  if (shared_memory == nullptr) {
    context.SetReturnValues(0, 0);
  } else {
    context.SetReturnValues(shared_memory->shared_memory->id,
                            shared_memory->virtual_address);
  }
}

void JoinSharedMemory(SyscallContext& context) {
  if (!context.has_thread()) return;

  SharedMemoryInProcess* shared_memory =
      ::ipc::JoinSharedMemory(context.process(), context.arg0());
  if (shared_memory == nullptr) {
    context.SetReturnValues(0, 0, 0);
  } else {
    context.SetReturnValues(shared_memory->shared_memory->size_in_pages,
                            shared_memory->virtual_address,
                            shared_memory->shared_memory->flags);
  }
}

void JoinChildProcessInSharedMemory(SyscallContext& context) {
  if (!context.has_thread()) return;

  ProcessRef child_process = GetProcessFromPid(context.arg0());
  context.ReturnBoolean(::ipc::JoinChildProcessInSharedMemory(
      context.process(), child_process.get(), context.arg1(), context.arg2()));

}

void LeaveSharedMemory(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::LeaveSharedMemory(context.process(), context.arg0());
  }
}

void GetSharedMemoryDetails(SyscallContext& context) {
  if (context.has_thread()) {
    GetSharedMemoryDetailsPertainingToProcess(
        context.process(), context.arg0(), context.registers().rax,
        context.registers().rbx);
  }
}

void MovePageIntoSharedMemory(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::MovePageIntoSharedMemory(context.process(), context.arg0(),
                                   context.arg1(), context.arg2());
  }
}

void GrantPermissionToAllocateIntoSharedMemory(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::GrantPermissionToAllocateIntoSharedMemory(
        context.process(), context.arg0(), context.arg1());
  }
}

void IsSharedMemoryPageAllocated(SyscallContext& context) {
  if (!context.has_thread()) return;

  context.ReturnBoolean(IsAddressAllocatedInSharedMemory(context.arg0(),
                                                         context.arg1()));
}

void GetSharedMemoryPagePhysicalAddress(SyscallContext& context) {
  if (!context.has_thread()) return;

  if (context.process()->is_driver) {
    context.Return(GetPhysicalAddressOfPageInSharedMemory(context.arg0(),
                                                          context.arg1()));
  } else {
    context.Return(kOutOfMemory);
  }
}

void GrowSharedMemory(SyscallContext& context) {
  if (!context.has_thread()) return;

  SharedMemoryInProcess* shared_memory =
      ::ipc::GrowSharedMemory(context.process(), context.arg0(), context.arg1());
  if (shared_memory == nullptr) {
    context.SetReturnValues(0, 0);
  } else {
    context.SetReturnValues(shared_memory->shared_memory->size_in_pages,
                            shared_memory->virtual_address);
  }
}

void RegisterSharedMemoryEvent(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::RegisterSharedMemoryEvent(context.process(), context.arg0(),
                                     context.arg1(), context.arg2());
  }
}

void UnregisterSharedMemoryEvent(SyscallContext& context) {
  if (context.has_thread()) {
    ::ipc::UnregisterSharedMemoryEvent(context.process(), context.arg0(),
                                       context.arg1());
  }
}

void TriggerSharedMemoryEvent(SyscallContext& context) {
  if (!context.has_thread()) return;

  ::ipc::TriggerSharedMemoryEvent(context.arg0(), context.arg1());
}

}  // namespace syscall
#endif  // TEST
