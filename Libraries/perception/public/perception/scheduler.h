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

// Defers running a function.
void Defer(const std::function<void()>& function);

// Hand over control to the scheduler. This function never returns.
void HandOverControl();

// Runs all fibers, handles all events, then returns when there's
// nothing else to do.
void FinishAnyPendingWork();

// Sleeps until a message is received, then handles all messages and
// events and returns where there's nothing else to do.
void WaitForMessagesThenReturn();

class Scheduler {
public:
	// Gets the next fiber to run, which might sleep if there's nothing
	// else to do.
	static Fiber* GetNextFiberToRun();

	// Schedules a fiber to run.
	static void ScheduleFiber(Fiber* fiber);

private:

	// Returns a fiber to handle the message, or nullptr if there's
	// nothing to do.
	static Fiber* GetFiberToHandleMessage(
		ProcessId senders_pid, MessageId message_id,
		size_t metadata, size_t param1, size_t param2, size_t param3,
		size_t param4, size_t param5);

};

}