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
#include "syscall/memory_syscalls.h"

#include "ipc/shared_memory.h"
#include "memory/memory.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#include "processes/process.h"
#include "scheduling/cpu_core.h"
#include "scheduling/thread.h"
#include "scheduling/timer.h"

using memory::g_free_pages;
using memory::g_total_system_memory;
using memory::kMaxPagesPerSyscall;
using memory::kPageSize;
using processes::GetProcessFromPid;
using processes::Process;
using processes::ProcessRef;
using scheduling::CatchUpProcessCpuUsage;
using scheduling::g_active_core_count;
using scheduling::IsCpuTrackingActive;
using scheduling::kMaxCores;

namespace syscall {

void AllocateMemoryPages(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t pages_requested = context.arg0();
  size_t result =
      context.process()->virtual_address_space.AllocatePages(pages_requested);
  context.Return(result);
}

void AllocateMemoryPagesBelowPhysicalBase(SyscallContext& context) {
  if (!context.has_thread()) return;

  if (context.process()->is_driver) {
    size_t pages_requested = context.arg0();
    size_t max_base = context.arg1();
    size_t result = context.process()
                        ->virtual_address_space.AllocatePagesBelowMaxBaseAddress(
                            pages_requested, max_base);
    size_t physical_addr =
        context.process()->virtual_address_space.GetPhysicalAddress(
            result, /*ignore_unowned_pages=*/false);
    context.SetReturnValues(result, physical_addr);
  } else {
    context.SetReturnValues(kOutOfMemory, 0);
  }
}

void ReleaseMemoryPages(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t pages_to_free = context.arg1();
  context.process()->virtual_address_space.FreePages(context.arg0(),
                                                    pages_to_free);
}

void MapPhysicalMemory(SyscallContext& context) {
  if (!context.has_thread()) return;

  if (context.process()->is_driver) {
    context.Return(
        context.process()->virtual_address_space.MapPhysicalPages(
            context.arg0(), context.arg1()));
  } else {
    context.Return(kOutOfMemory);
  }
}

void GetPhysicalAddressOfVirtualAddress(SyscallContext& context) {
  if (!context.has_thread()) return;

  if (context.process()->is_driver) {
    context.Return(
        context.process()->virtual_address_space.GetPhysicalAddress(
            context.arg0(), /*ignore_unowned_pages=*/false));
  } else {
    context.Return(0);
  }
}

void GetSystemMetrics(SyscallContext& context) {
  if (!context.has_thread()) return;

  context.SetReturnValues(
      g_total_system_memory, ipc::GetAllocatedSharedMemoryInBytes(),
      g_free_pages * kPageSize, g_active_core_count);
}

void GetProcessHealthMetrics(SyscallContext& context) {
  if (!context.has_thread()) return;

  ProcessRef process_ref;
  Process* process = nullptr;
  if (context.arg0() == 0) {
    process = context.process();
  } else {
    process_ref = GetProcessFromPid(context.arg0());
    process = process_ref.get();
  }


  if (process == nullptr) {
    context.Return(0);
    context.registers().rbx = 0;
    context.registers().rdx = 0;
    context.registers().rsi = 0;
    context.set_rdi(0);
    context.registers().r8 = 0;
    context.registers().r9 = 0;
    context.registers().r10 = 0;
    context.set_r12(0);
    context.set_r13(0);
    context.set_r14(0);
    context.set_r15(0);
    return;
  }

  if (IsCpuTrackingActive()) CatchUpProcessCpuUsage(process);

  context.Return(process->virtual_address_space.GetUniquePages() * kPageSize);
  context.registers().rbx = process->creation_timestamp;

  auto pack_cores = [&](int start_core) -> size_t {
    size_t packed = 0;
    for (int i = 0; i < 8; i++) {
      int core = start_core + i;
      if (core < kMaxCores) {
        packed |=
            (static_cast<size_t>(process->rolling_cpu_percentage[core])
             << (i * 8));
      }
    }
    return packed;
  };

  context.registers().rdx = pack_cores(0);
  context.registers().r8 = pack_cores(8);
  context.registers().r9 = pack_cores(16);
  context.registers().r10 = pack_cores(24);
  context.set_r12(pack_cores(32));
  context.set_r13(pack_cores(40));
  context.set_r14(pack_cores(48));
  context.set_r15(pack_cores(56));

  context.registers().rsi = process->service_count;
  context.set_rdi(process->virtual_address_space.GetSharedPages() * kPageSize);
}

void SetMemoryAccessRights(SyscallContext& context) {
  if (!context.has_thread()) return;

  size_t address = context.arg0();
  size_t num_pages = context.arg1();
  if (num_pages == 0 || num_pages > kMaxPagesPerSyscall) return;
  size_t bytes = num_pages * kPageSize;
  if (bytes / kPageSize != num_pages || address + bytes < address) return;

  size_t max_address = address + bytes;
  size_t rights = context.arg2();

  for (; address < max_address; address += kPageSize) {
    context.process()->virtual_address_space.SetMemoryAccessRights(address,
                                                                  rights);
  }
}

}  // namespace syscall
#endif  // TEST
