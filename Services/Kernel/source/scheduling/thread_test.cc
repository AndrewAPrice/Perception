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

#include "processes/process.h"
#include "scheduling/thread.h"
#include "memory/virtual_allocator.h"
#include "testing.h"
#include "containers/object_pools.h"

using containers::InitializeObjectPools;
using memory::InitializeVirtualAllocator;
using memory::TemporarilyMapPhysicalPages;
using processes::CreateChildProcess;
using processes::CreateProcess;
using processes::DestroyProcess;
using processes::GetProcessFromPid;
using processes::InitializeProcesses;
using processes::IsProcessAChildOfParent;
using processes::Process;
using scheduling::CpuCoreState;
using scheduling::CreateThread;
using scheduling::DestroyThread;
using scheduling::g_cpu_cores;
using scheduling::GetThreadFromTid;
using scheduling::InitializeCpuCoreState;
using scheduling::InitializeScheduler;
using scheduling::InitializeThreads;
using scheduling::Scheduler;
using scheduling::ScheduleThread;
using scheduling::Thread;
using scheduling::ThreadPriority;
using scheduling::ThreadState;
using scheduling::UnscheduleThread;

TEST(ThreadLifecycleAndReclamationTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  Process* p1 = CreateProcess(false, true);
  ASSERT(p1 != nullptr, true);

  // Create child process
  Process* child = CreateChildProcess(p1, (char*)"MyChild", 0);
  ASSERT(child != nullptr, true);
  ASSERT(IsProcessAChildOfParent(p1, child), true);
  ASSERT(child->parent, p1);

  // Create thread inside the child process
  Thread* t1 = CreateThread(child, 0x1000, 42);
  ASSERT(t1 != nullptr, true);
  ASSERT(child->threads.count(), (size_t)1);
  ASSERT(GetThreadFromTid(child, t1->id), t1);

  // Destroy the last thread. This should automatically trigger child process
  // destruction!
  size_t child_pid = child->pid;
  DestroyThread(t1, /*process_being_destroyed=*/false);

  // Verify child process was automatically reclaimed
  ASSERT(GetProcessFromPid(child_pid) == nullptr, true);
  ASSERT(IsProcessAChildOfParent(p1, child), false);

  // Clean up parent
  DestroyProcess(p1);
}

TEST(ThreadAddressClearOnTerminationTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  Process* p1 = CreateProcess(false, true);
  ASSERT(p1 != nullptr, true);

  // Allocate a page in p1.
  size_t page_addr = p1->virtual_address_space.AllocatePages(1);
  ASSERT(page_addr != kOutOfMemory, true);

  // Write a non-zero value to the page.
  size_t offset = 16;
  size_t target_address = page_addr + offset;
  
  size_t physical_page = p1->virtual_address_space.GetPhysicalAddress(page_addr, false);
  ASSERT(physical_page != kOutOfMemory, true);
  
  volatile uint64* ptr = (volatile uint64*)((size_t)TemporarilyMapPhysicalPages(physical_page, 1) + offset);
  *ptr = 0xDEADBEEF;
  ASSERT(*ptr, (uint64)0xDEADBEEF);

  // Create a thread and set address_to_clear_on_termination.
  Thread* t1 = CreateThread(p1, 0x1000, 42);
  ASSERT(t1 != nullptr, true);
  t1->address_to_clear_on_termination = target_address;

  // Destroy the thread. This automatically destroys p1 as it is the last thread.
  DestroyThread(t1, /*process_being_destroyed=*/false);

  // Verify that the target address is now cleared to 0.
  ptr = (volatile uint64*)((size_t)TemporarilyMapPhysicalPages(physical_page, 1) + offset);
  ASSERT(*ptr, 0ULL);
}

TEST(ThreadIsolationTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  // Create two distinct processes.
  Process* p1 = CreateProcess(false, true);
  Process* p2 = CreateProcess(false, true);
  ASSERT(p1 != nullptr, true);
  ASSERT(p2 != nullptr, true);

  // Create a thread in each process.
  Thread* t1 = CreateThread(p1, 0x1000, 42);
  Thread* t2 = CreateThread(p2, 0x1000, 43);
  ASSERT(t1 != nullptr, true);
  ASSERT(t2 != nullptr, true);

  // Verify that process 1 can see its own thread but not process 2's thread.
  ASSERT(GetThreadFromTid(p1, t1->id), t1);
  ASSERT(GetThreadFromTid(p1, t2->id) == nullptr, true);

  // Verify that process 2 can see its own thread but not process 1's thread.
  ASSERT(GetThreadFromTid(p2, t2->id), t2);
  ASSERT(GetThreadFromTid(p2, t1->id) == nullptr, true);

  // Clean up
  DestroyThread(t1, /*process_being_destroyed=*/false);
  DestroyThread(t2, /*process_being_destroyed=*/false);
}

TEST(ThreadStateTransitionsTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  Process* p = CreateProcess(false, false);
  ASSERT(p != nullptr, true);

  Thread* t = CreateThread(p, 0x1000, 0);
  ASSERT(t != nullptr, true);

  // Newly created thread starts in Halted state.
  ASSERT(t->state == ThreadState::Halted, true);
  ASSERT(t->is_halted(), true);
  ASSERT(t->is_awake(), false);
  ASSERT(t->running_on_core, -1);

  // Transition to Ready.
  t->TransitionToReady();
  ASSERT(t->state == ThreadState::Ready, true);
  ASSERT(t->is_ready(), true);
  ASSERT(t->is_awake(), true);
  ASSERT(t->running_on_core, -1);

  // Transition to Running on core 3.
  t->TransitionToRunning(3);
  ASSERT(t->state == ThreadState::Running, true);
  ASSERT(t->is_running(), true);
  ASSERT(t->is_awake(), true);
  ASSERT(t->running_on_core, 3);

  // Transition to BlockedOnMessage. running_on_core remains set until context switch completes.
  t->TransitionToBlockedOnMessage();
  ASSERT(t->state == ThreadState::BlockedOnMessage, true);
  ASSERT(t->is_waiting_for_message(), true);
  ASSERT(t->is_awake(), false);
  ASSERT(t->running_on_core, 3);

  // Transition to BlockedOnMemory.
  t->TransitionToBlockedOnMemory(nullptr);
  ASSERT(t->state == ThreadState::BlockedOnMemory, true);
  ASSERT(t->is_waiting_for_shared_memory(), true);
  ASSERT(t->is_awake(), false);
  ASSERT(t->running_on_core, 3);

  // Transition back to Halted.
  t->TransitionToHalted();
  ASSERT(t->state == ThreadState::Halted, true);
  ASSERT(t->is_halted(), true);
  ASSERT(t->is_awake(), false);

  // Scheduling transitions to Ready.
  ScheduleThread(t);
  ASSERT(t->state == ThreadState::Ready, true);
  ASSERT(t->is_ready(), true);
  ASSERT(t->is_awake(), true);

  // Unscheduling with BlockedOnMessage transitions to BlockedOnMessage.
  UnscheduleThread(t, ThreadState::BlockedOnMessage);
  ASSERT(t->state == ThreadState::BlockedOnMessage, true);
  ASSERT(t->is_waiting_for_message(), true);
  ASSERT(t->is_awake(), false);

  // Re-scheduling transitions back to Ready.
  ScheduleThread(t);
  ASSERT(t->state == ThreadState::Ready, true);
  ASSERT(t->is_awake(), true);

  // Unscheduling with BlockedOnMemory transitions to BlockedOnMemory.
  UnscheduleThread(t, ThreadState::BlockedOnMemory);
  ASSERT(t->state == ThreadState::BlockedOnMemory, true);
  ASSERT(t->is_waiting_for_shared_memory(), true);
  ASSERT(t->is_awake(), false);

  // Re-scheduling transitions back to Ready.
  ScheduleThread(t);
  ASSERT(t->state == ThreadState::Ready, true);

  // Unscheduling with default (Halted).
  UnscheduleThread(t);
  ASSERT(t->state == ThreadState::Halted, true);
  ASSERT(t->is_halted(), true);
  ASSERT(t->is_awake(), false);

  // Transition to Terminated.
  t->TransitionToTerminated();
  ASSERT(t->state == ThreadState::Terminated, true);
  ASSERT(t->is_terminated(), true);
  // Simulate core 3 finishing context switch away from the thread.
  t->running_on_core = -1;
  DestroyThread(t, false);
}

TEST(SchedulerAndCpuCoreMethodsTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();
  InitializeScheduler();

  Process* p = CreateProcess(false, false);
  ASSERT(p != nullptr, true);

  Thread* t = CreateThread(p, 0x1000, 0);
  ASSERT(t != nullptr, true);

  // Test Scheduler singleton methods
  Scheduler::Get().Schedule(t);
  ASSERT(t->is_ready(), true);

  Scheduler::Get().SetPriority(t, ThreadPriority::InteractiveApp);
  ASSERT(t->priority, ThreadPriority::InteractiveApp);

  Scheduler::Get().SetFocusedProcess(p);
  ASSERT(Scheduler::Get().GetFocusedProcess(), p);

  Scheduler::Get().Unschedule(t, ThreadState::Halted);
  ASSERT(t->is_halted(), true);

  // Test CpuCoreState query methods
  InitializeCpuCoreState(0, 0);
  CpuCoreState& core0 = g_cpu_cores[0];
  ASSERT(core0.id(), (uint32)0);
  ASSERT(core0.apic(), (uint32)0);
  ASSERT(core0.online(), true);
  ASSERT(core0.is_idle(), true);

  core0.set_running_thread(t);
  ASSERT(core0.is_idle(), false);
  ASSERT(core0.current_thread(), t);
  core0.set_running_thread(nullptr);
  ASSERT(core0.is_idle(), true);

  DestroyThread(t, false);
}

