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

#include "ipc/messages.h"
#include "processes/process.h"
#include "scheduling/thread.h"
#include "memory/virtual_allocator.h"
#include "testing.h"
#include "containers/object_pools.h"
#include "hardware/registers.h"
#include <unordered_map>

using containers::InitializeObjectPools;
using hardware::Registers;
using ipc::GetNextQueuedMessage;
using ipc::LoadNextMessageIntoThread;
using ipc::Message;
using ipc::MessageType;
using ipc::SendMessageFromThreadSyscall;
using ipc::SleepThreadUntilMessage;
using memory::InitializeVirtualAllocator;
using processes::CreateProcess;
using processes::DestroyProcess;
using processes::InitializeProcesses;
using processes::Process;
using processes::SetChildProcessMemoryPages;
using scheduling::CreateThread;
using scheduling::InitializeThreads;
using scheduling::Thread;

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
  ASSERT(regs_t1.rax, (size_t)0); // Status::OK = 0
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

TEST(MessagesDirectRegisterDeliveryTest) {
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

  // Put t2 to sleep waiting for an incoming message.
  bool slept = SleepThreadUntilMessage(t2);
  ASSERT(slept, true);
  ASSERT(t2->is_waiting_for_message(), true);

  // Set up t1 registers to send one-way message.
  Registers& regs_t1 = t1->registers;
  regs_t1.rbx = p2->pid;
  regs_t1.rax = 999;
  regs_t1.rdx = 0;
  regs_t1.rsi = 123;
  regs_t1.r8 = 456;
  regs_t1.r9 = 789;
  regs_t1.r10 = 101112;
  regs_t1.r12 = 131415;

  // Send message via thread syscall simulation.
  SendMessageFromThreadSyscall(t1);

  // Verify sender succeeded.
  ASSERT(regs_t1.rax, (size_t)0);

  // Message should have been delivered directly: 0 messages queued.
  ASSERT(p2->messages_queued, (size_t)0);

  // Receiver thread should have had registers populated directly.
  Registers& regs_t2 = t2->registers;
  ASSERT(t2->is_waiting_for_message(), false);
  ASSERT(regs_t2.rax, (size_t)999);
  ASSERT(regs_t2.rbx, p1->pid);
  ASSERT(regs_t2.rsi, (size_t)123);
  ASSERT(regs_t2.r8, (size_t)456);
  ASSERT(regs_t2.r9, (size_t)789);
  ASSERT(regs_t2.r10, (size_t)101112);
  ASSERT(regs_t2.r12, (size_t)131415);

  // Clean up
  DestroyProcess(p1);
  DestroyProcess(p2);
}

TEST(MessagesRpcCallAndResponseDirectHandoverTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  Process* p1 = CreateTestProcess("ClientProcess");
  Process* p2 = CreateTestProcess("ServerProcess");
  ASSERT(p1 != nullptr, true);
  ASSERT(p2 != nullptr, true);

  Thread* t1 = CreateThread(p1, 0x1000, 0);
  Thread* t2 = CreateThread(p2, 0x2000, 0);
  ASSERT(t1 != nullptr, true);
  ASSERT(t2 != nullptr, true);

  // Server thread t2 sleeps waiting for incoming RPC calls.
  bool slept = SleepThreadUntilMessage(t2);
  ASSERT(slept, true);

  // Client t1 sends RPC call (message_type == 1).
  Registers& regs_t1 = t1->registers;
  regs_t1.rbx = p2->pid;
  regs_t1.rax = 5001; // Call message ID
  regs_t1.rdx = static_cast<size_t>(MessageType::Call);
  regs_t1.rsi = 6001; // Response message ID
  regs_t1.r8 = 42;    // Parameter

  SendMessageFromThreadSyscall(t1);
  ASSERT(regs_t1.rax, (size_t)0);

  // Verify server t2 received call directly with synthetic response ID in rsi.
  Registers& regs_t2 = t2->registers;
  ASSERT(t2->is_waiting_for_message(), false);
  ASSERT(regs_t2.rax, (size_t)5001);
  ASSERT(regs_t2.rbx, p1->pid);
  ASSERT(regs_t2.r8, (size_t)42);
  size_t synthetic_response_id = regs_t2.rsi;

  // Client t1 now sleeps waiting for RPC response.
  bool client_slept = SleepThreadUntilMessage(t1);
  ASSERT(client_slept, true);

  // Server t2 responds to the call (message_type == 2).
  regs_t2.rbx = p1->pid;
  regs_t2.rax = synthetic_response_id;
  regs_t2.rdx = static_cast<size_t>(MessageType::Response);
  regs_t2.rsi = 9999; // Return status/payload

  SendMessageFromThreadSyscall(t2);
  ASSERT(regs_t2.rax, (size_t)0);

  // Verify client t1 received response directly into registers.
  ASSERT(t1->is_waiting_for_message(), false);
  ASSERT(regs_t1.rax, (size_t)6001); // Original response message ID
  ASSERT(regs_t1.rbx, p2->pid);      // Server PID
  ASSERT(regs_t1.rsi, (size_t)9999); // Return status/payload

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
  p2->parent = p1;
  p2->next_child_process_in_parent = p1->child_processes;
  p1->child_processes = p2;
  ASSERT(p1 != nullptr, true);
  ASSERT(p2 != nullptr, true);

  Thread* t1 = CreateThread(p1, 0x1000, 0);
  ASSERT(t1 != nullptr, true);

  // Map virtual address 0x700000 in p1 to physical address 0x8000000
  size_t src_virtual = 0x700000;
  size_t phys_addr = 0x8000000;
  p1->virtual_address_space.ReserveAddressRange(src_virtual, 1);
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
  ASSERT(regs_t1.rax, (size_t)0); // Status::OK = 0

  // Perform memory page transfer to child process p2
  size_t dest_virtual = 0x900000;
  SetChildProcessMemoryPages(p1, p2, src_virtual, dest_virtual, 1);

  // Verify the page was unmapped from p1
  size_t p1_lookup = p1->virtual_address_space.GetPhysicalAddress(src_virtual, false);
  ASSERT(p1_lookup, (size_t)kOutOfMemory);

  // Retrieve and verify the message in p2
  ASSERT(p2->messages_queued, (size_t)1);
  Message* msg = GetNextQueuedMessage(p2);
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)888);

  // Verify the page was successfully mapped into p2 at the same physical address
  size_t p2_lookup = p2->virtual_address_space.GetPhysicalAddress(dest_virtual, false);
  ASSERT(p2_lookup, phys_addr);

  // Verify data remains intact inside the page
  char* p2_data = (char*)simulated_ram[p2_lookup].entries;
  int match = (p2_data[0] == 'A' && p2_data[1] == 'B');
  ASSERT(match, 1);

  // Clean up
  DestroyProcess(p1);
}
