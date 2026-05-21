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
#include "virtual_allocator.h"
#include "testing.h"
#include "object_pools.h"

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
  Process* looked_up = GetProcessFromPid(p1->pid);
  ASSERT(looked_up, p1);

  // Clean up
  DestroyProcess(p1);
  ASSERT(AreAnyProcessesRunning(), false);
}
