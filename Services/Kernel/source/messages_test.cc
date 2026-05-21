// Copyright 2026 Google LLC
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

#include "messages.h"
#include "process.h"
#include "thread.h"
#include "virtual_allocator.h"
#include "testing.h"
#include "object_pools.h"
#include "registers.h"
#include <unordered_map>

struct PhysicalPageBuffer {
  size_t entries[512];
};

extern std::unordered_map<size_t, PhysicalPageBuffer> simulated_ram;

namespace {

Process* CreateTestProcess(const char* name) {
  Process* p = CreateProcess(false, false);
  return p;
}

}  // namespace

TEST(MessagesSyscallMessagingTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  Process* p1 = CreateTestProcess("Process1");
  Process* p2 = CreateTestProcess("Process2");
  ASSERT(p1 != nullptr, true);
  ASSERT(p2 != nullptr, true);

  Thread* t1 = CreateThread(p1, 0x1000, 0);
  Thread* t2 = CreateThread(p2, 0x2000, 0);
  ASSERT(t1 != nullptr, true);
  ASSERT(t2 != nullptr, true);

  // Set up thread registers to send message
  Registers& regs_t1 = t1->registers;
  regs_t1.rbx = p2->pid; // receiver PID
  regs_t1.rax = 777; // message ID
  regs_t1.rdx = 0; // no paging
  regs_t1.rsi = 111; // param 1
  regs_t1.r8 = 222; // param 2

  // Send message via thread syscall simulation
  SendMessageFromThreadSyscall(t1);
  
  // Verify success status in rax register
  ASSERT(regs_t1.rax, (size_t)0); // MS_SUCCESS = 0
  ASSERT(p2->messages_queued, (size_t)1);

  // Load the message into thread t2
  LoadNextMessageIntoThread(t2);

  // Verify receiver thread registers
  Registers& regs_t2 = t2->registers;
  ASSERT(regs_t2.rax, (size_t)777); // message ID
  ASSERT(regs_t2.rbx, p1->pid); // sender PID
  ASSERT(regs_t2.rsi, (size_t)111); // param 1
  ASSERT(regs_t2.r8, (size_t)222); // param 2 (passed in r8)

  // Clean up
  DestroyProcess(p1);
  DestroyProcess(p2);
}

TEST(MessagesSyscallPagingTransferTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  Process* p1 = CreateTestProcess("Process1");
  Process* p2 = CreateTestProcess("Process2");
  ASSERT(p1 != nullptr, true);
  ASSERT(p2 != nullptr, true);

  Thread* t1 = CreateThread(p1, 0x1000, 0);
  ASSERT(t1 != nullptr, true);

  // Map virtual address 0x700000 in p1 to physical address 0x8000000
  size_t src_virtual = 0x700000;
  size_t phys_addr = 0x8000000;
  bool success = p1->virtual_address_space.MapPhysicalPageAt(
      src_virtual, phys_addr, /*own=*/true, /*can_write=*/true,
      /*throw_exception_on_access=*/false);
  
  ASSERT(success, true);

  // Write test bytes to simulated physical RAM page
  char* ram_data = (char*)simulated_ram[phys_addr].entries;
  ram_data[0] = 'A';
  ram_data[1] = 'B';
  ram_data[2] = '\0';

  // Configure thread t1 registers to send paging message
  Registers& regs_t1 = t1->registers;
  regs_t1.rbx = p2->pid; // receiver PID
  regs_t1.rax = 888; // message ID
  regs_t1.rdx = 1; // Paging metadata bit 0 = true
  regs_t1.r10 = src_virtual; // source virtual address
  regs_t1.r12 = 1; // size in pages

  // Send message
  SendMessageFromThreadSyscall(t1);

  // Verify success
  ASSERT(regs_t1.rax, (size_t)0); // MS_SUCCESS = 0

  // Verify the page was unmapped from p1
  size_t p1_lookup = p1->virtual_address_space.GetPhysicalAddress(src_virtual, false);
  ASSERT(p1_lookup, (size_t)OUT_OF_MEMORY);

  // Retrieve and verify the message in p2
  ASSERT(p2->messages_queued, (size_t)1);
  Message* msg = GetNextQueuedMessage(p2);
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)888);
  ASSERT(msg->param5, (size_t)1); // size in pages
  
  size_t dest_virtual = msg->param4; // target virtual address allocated by kernel in p2
  ASSERT(dest_virtual != 0, true);

  // Verify the page was successfully mapped into p2 at the same physical address
  size_t p2_lookup = p2->virtual_address_space.GetPhysicalAddress(dest_virtual, false);
  ASSERT(p2_lookup, phys_addr);

  // Verify data remains intact inside the page
  char* p2_data = (char*)simulated_ram[p2_lookup].entries;
  int match = (p2_data[0] == 'A' && p2_data[1] == 'B');
  ASSERT(match, 1);

  // Clean up
  DestroyProcess(p1);
  DestroyProcess(p2);
}
