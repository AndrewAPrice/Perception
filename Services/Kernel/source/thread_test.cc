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

#include "process.h"
#include "thread.h"
#include "virtual_allocator.h"
#include "testing.h"
#include "object_pools.h"

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
  ASSERT(child->thread_count, (unsigned short)1);
  ASSERT(GetThreadFromTid(child, t1->id), t1);

  // Destroy the last thread. This should automatically trigger child process destruction!
  DestroyThread(t1, /*process_being_destroyed=*/false);

  // Verify child process was automatically reclaimed
  ASSERT(GetProcessFromPid(child->pid) == nullptr, true);
  ASSERT(IsProcessAChildOfParent(p1, child), false);

  // Clean up parent
  DestroyProcess(p1);
}
