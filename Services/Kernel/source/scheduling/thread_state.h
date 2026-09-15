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

#pragma once

#include "types.h"

namespace scheduling {

// Priority levels for thread scheduling.
enum class ThreadPriority : uint8 {
  InterruptDriver = 0,  // Hardware drivers
  RealtimeService = 1,  // UI Compositor / Window Manager
  InteractiveApp = 2,   // Focused GUI App (foreground)
  Normal = 3,           // Background services (Net Manager, etc.)
  Background = 4,       // CPU-heavy processes (compilers, etc.)
  Idle = 5              // Strictly idle tasks (backups, etc.)
};

// Mutually exclusive execution states of a thread.
enum class ThreadState : uint8 {
  // The thread is created or halted and not queued for execution.
  Halted = 0,

  // The thread is in a ready queue and eligible for scheduling.
  Ready = 1,

  // The thread is actively executing on a CPU core.
  Running = 2,

  // The thread is sleeping waiting for an incoming message.
  BlockedOnMessage = 3,

  // The thread is sleeping waiting for a shared memory page.
  BlockedOnMemory = 4,

  // The thread is marked for termination.
  Terminated = 5
};

// The number of thread priority levels.
constexpr int kThreadPriorityCount = 6;

}  // namespace scheduling
