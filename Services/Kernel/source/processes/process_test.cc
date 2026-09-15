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
#include "processes/process_manager.h"
#include "scheduling/thread.h"
#include "ipc/service.h"
#include "memory/virtual_allocator.h"
#include "testing.h"
#include "containers/object_pools.h"

using containers::InitializeObjectPools;
using memory::InitializeVirtualAllocator;
using processes::AreAnyProcessesRunning;
using processes::CreateProcess;
using processes::DestroyProcess;
using processes::GetProcessFromPid;
using processes::InitializeProcesses;
using processes::Process;
using processes::ProcessManager;
using processes::ProcessRef;

TEST(ProcessLifecycleTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  // Assert initial state
  ASSERT(AreAnyProcessesRunning(), false);

  // Create a new process p1
  Process* p1 = CreateProcess(false, false);
  ASSERT(p1 != nullptr, true);
  ASSERT(AreAnyProcessesRunning(), true);

  // Verify lookup
  ProcessRef looked_up = GetProcessFromPid(p1->pid);
  ASSERT(looked_up.get(), p1);


  // Clean up
  DestroyProcess(p1);
  ASSERT(AreAnyProcessesRunning(), false);
}

TEST(ProcessServicesCountTest) {
  InitializeObjectPools();
  InitializeProcesses();
  ipc::InitializeServices();

  Process* p1 = CreateProcess(false, false);
  ASSERT(p1 != nullptr, true);

  // Initially 0 services
  ASSERT(p1->service_count, (size_t)0);

  // Register a service
  ipc::RegisterService((char*)"my_cool_service", p1, 101);
  ASSERT(p1->service_count, (size_t)1);

  // Register another service
  ipc::RegisterService((char*)"another_service", p1, 102);
  ASSERT(p1->service_count, (size_t)2);

  // Unregister one service
  ipc::UnregisterServiceByMessageId(p1, 101);
  ASSERT(p1->service_count, (size_t)1);

  DestroyProcess(p1);
}

TEST(ProcessManagerChildTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  Process* parent = ProcessManager::Get().CreateProcess(false, true);
  ASSERT(parent != nullptr, true);

  Process* child = ProcessManager::Get().CreateChildProcess(
      parent, (char*)"child_proc", 0);
  ASSERT(child != nullptr, true);
  ASSERT(ProcessManager::Get().IsProcessAChildOfParent(parent, child), true);

  Process* not_child = ProcessManager::Get().CreateProcess(false, false);
  ASSERT(ProcessManager::Get().IsProcessAChildOfParent(parent, not_child), false);

  ProcessManager::Get().DestroyChildProcess(parent, child);
  ASSERT(ProcessManager::Get().IsProcessAChildOfParent(parent, child), false);

  ProcessManager::Get().DestroyProcess(not_child);
  ProcessManager::Get().DestroyProcess(parent);
}

TEST(ProcessThreadsAndMessageQueueTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  Process* proc = CreateProcess(false, false);
  ASSERT(proc != nullptr, true);

  // Test MessageQueue operations
  ASSERT(proc->message_queue.count(), (size_t)0);
  ASSERT(proc->message_queue.is_empty(), true);

  scheduling::Thread* waiting_thread = nullptr;
  {
    containers::InterruptSafeSpinlockGuard guard(proc->message_queue.lock());
    Status status = proc->message_queue.DeliverOrQueueLocked(
        42, 1, 0, 10, 20, 30, 40, 50, waiting_thread);
    ASSERT(status, Status::OK);
  }
  ASSERT(proc->message_queue.count(), (size_t)1);
  ASSERT(proc->message_queue.is_empty(), false);

  ipc::Message* msg = nullptr;
  {
    containers::InterruptSafeSpinlockGuard guard(proc->message_queue.lock());
    msg = proc->message_queue.PopNextLocked();
  }
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)42);
  ASSERT(proc->message_queue.count(), (size_t)0);
  containers::ObjectPool<ipc::Message>::Release(msg);

  // Test ProcessThreads operations
  ASSERT(proc->threads.count(), (size_t)0);
  ASSERT(proc->threads.IsEmpty(), true);

  scheduling::Thread* thread = scheduling::CreateThread(proc, 0x1000, 0);
  ASSERT(thread != nullptr, true);
  ASSERT(proc->threads.count(), (size_t)1);
  ASSERT(proc->threads.IsEmpty(), false);
  ASSERT(proc->threads.Get(thread->id), thread);
  ASSERT(proc->threads.FirstItem(), thread);

  size_t iterated_threads = 0;
  for (scheduling::Thread* t : proc->threads) {
    ASSERT(t, thread);
    iterated_threads++;
  }
  ASSERT(iterated_threads, (size_t)1);

  scheduling::DestroyThread(thread, false);
}

TEST(ProcessCanSetFocusTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  Process* parent = ProcessManager::Get().CreateProcess(false, true);
  ASSERT(parent != nullptr, true);

  Process* child_no_focus = ProcessManager::Get().CreateChildProcess(
      parent, (char*)"child_no_focus", 0);
  ASSERT(child_no_focus != nullptr, true);
  ASSERT(child_no_focus->can_set_focus, false);

  Process* child_with_focus = ProcessManager::Get().CreateChildProcess(
      parent, (char*)"child_with_focus", 1 << 2);
  ASSERT(child_with_focus != nullptr, true);
  ASSERT(child_with_focus->can_set_focus, true);

  ProcessManager::Get().DestroyChildProcess(parent, child_no_focus);
  ProcessManager::Get().DestroyChildProcess(parent, child_with_focus);
  ProcessManager::Get().DestroyProcess(parent);
}

TEST(ProcessCanTerminateProcessesTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  Process* parent = ProcessManager::Get().CreateProcess(false, true);
  ASSERT(parent != nullptr, true);

  Process* child_no_terminate = ProcessManager::Get().CreateChildProcess(
      parent, (char*)"child_no_term", 0);
  ASSERT(child_no_terminate != nullptr, true);
  ASSERT(child_no_terminate->can_terminate_processes, false);

  Process* child_with_terminate = ProcessManager::Get().CreateChildProcess(
      parent, (char*)"child_with_term", 1 << 3);
  ASSERT(child_with_terminate != nullptr, true);
  ASSERT(child_with_terminate->can_terminate_processes, true);

  ProcessManager::Get().DestroyChildProcess(parent, child_no_terminate);
  ProcessManager::Get().DestroyChildProcess(parent, child_with_terminate);
  ProcessManager::Get().DestroyProcess(parent);
}

TEST(ProcessSys9DriverPrivilegeTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  // Non-driver parent cannot grant driver privileges to a child.
  Process* non_driver_parent = ProcessManager::Get().CreateProcess(false, true);
  ASSERT(non_driver_parent != nullptr, true);

  Process* child_of_non_driver = ProcessManager::Get().CreateChildProcess(
      non_driver_parent, (char*)"child_non_driver", 1 << 0);
  ASSERT(child_of_non_driver != nullptr, true);
  ASSERT(child_of_non_driver->is_driver, false);

  // Driver parent can grant driver privileges to a child.
  Process* driver_parent = ProcessManager::Get().CreateProcess(true, true);
  ASSERT(driver_parent != nullptr, true);

  Process* child_of_driver = ProcessManager::Get().CreateChildProcess(
      driver_parent, (char*)"child_driver", 1 << 0);
  ASSERT(child_of_driver != nullptr, true);
  ASSERT(child_of_driver->is_driver, true);

  ProcessManager::Get().DestroyChildProcess(non_driver_parent, child_of_non_driver);
  ProcessManager::Get().DestroyChildProcess(driver_parent, child_of_driver);
  ProcessManager::Get().DestroyProcess(non_driver_parent);
  ProcessManager::Get().DestroyProcess(driver_parent);
}

TEST(ProcessParentPidTrackingTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeVirtualAllocator();

  Process* parent = ProcessManager::Get().CreateProcess(false, true);
  ASSERT(parent != nullptr, true);

  Process* child = ProcessManager::Get().CreateChildProcess(
      parent, (char*)"child_proc", 0);
  ASSERT(child != nullptr, true);
  ASSERT(child->parent_pid, parent->pid);

  ProcessManager::Get().DestroyChildProcess(parent, child);
  ProcessManager::Get().DestroyProcess(parent);
}
