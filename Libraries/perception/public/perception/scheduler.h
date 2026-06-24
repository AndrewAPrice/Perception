// Copyright 2021 Google LLC
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

#include <functional>

#include "types.h"

namespace perception {

class Fiber;
struct MessageData;

// Defers running a function on the primary thread.
void Defer(const std::function<void()>& function);
void Defer(std::function<void()>&& function);

// Defers running a function on the primary thread until after all other
// deferred functions and incoming events have been handled.
void DeferAfterEvents(const std::function<void()>& function);
void DeferAfterEvents(std::function<void()>&& function);

// Runs a function in parallel in a new thread.
void DeferInParallel(const std::function<void()>& function);
void DeferInParallel(std::function<void()>&& function);

// Sets the focused process, that receives a temporary boost in priority. Only
// the Window Manager can call this.
void SetFocusedProcess(ProcessId pid);

// Hand over control to the scheduler. This function never returns and must only
// be called from the primary thread.
void HandOverControl();

// Runs all fibers, handles all events, then returns when there's
// nothing else to do. This function must only be called from the primary
// thread.
void FinishAnyPendingWork();

// Sleeps until a message is received, then handles all messages and
// events and returns where there's nothing else to do. This function must only
// be called from the primary thread.
void WaitForMessagesThenReturn();

class Scheduler {
 public:
  // Gets the next fiber to run, which might sleep if there's nothing
  // else to do.
  static Fiber* GetNextFiberToRun();

  // Schedules a fiber to run.
  static void ScheduleFiber(Fiber* fiber);

  // Schedules a fiber to run after all otehr fibers and incoming events
  // have been handled.
  static void ScheduleFiberAfterEvents(Fiber* fiber);

 private:
  // Returns a fiber to handle the message, or nullptr if there's
  // nothing to do.
  static Fiber* GetFiberToHandleMessage(ProcessId senders_pid,
                                        const MessageData& message_data);
};

}  // namespace perception