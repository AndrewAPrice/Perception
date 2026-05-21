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

#include "timer.h"

#include "messages.h"
#include "process.h"
#include "object_pools.h"
#include "testing.h"

namespace {

Process* CreateTestProcess(const char* name) {
  Process* p = CreateProcess(false, false);
  return p;
}

}  // namespace

TEST(TimerChronologicalExecutionTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeTimer();

  Process* p1 = CreateTestProcess("Process1");
  ASSERT(p1 != nullptr, true);

  // Current time starts at 0
  ASSERT(GetCurrentTimestampInMicroseconds(), (size_t)0);

  // Schedule events at t = 20000 and t = 40000
  SendMessageToProcessAtMicroseconds(p1, 20000, 101);
  SendMessageToProcessAtMicroseconds(p1, 40000, 102);

  // Tick 1: t goes to 10000
  TimerHandler();
  ASSERT(GetCurrentTimestampInMicroseconds(), (size_t)10000);
  ASSERT(p1->messages_queued, (size_t)0);

  // Tick 2: t goes to 20000
  TimerHandler();
  ASSERT(GetCurrentTimestampInMicroseconds(), (size_t)20000);
  ASSERT(p1->messages_queued, (size_t)1);

  Message* msg = GetNextQueuedMessage(p1);
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)101);

  // Tick 3: t goes to 30000
  TimerHandler();
  ASSERT(GetCurrentTimestampInMicroseconds(), (size_t)30000);
  ASSERT(p1->messages_queued, (size_t)0);

  // Tick 4: t goes to 40000
  TimerHandler();
  ASSERT(GetCurrentTimestampInMicroseconds(), (size_t)40000);
  ASSERT(p1->messages_queued, (size_t)1);

  msg = GetNextQueuedMessage(p1);
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)102);
}

TEST(TimerSortingIntegrityTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeTimer();

  Process* p1 = CreateTestProcess("Process1");
  ASSERT(p1 != nullptr, true);

  // Schedule events out-of-order: t=30000 first, then t=10000
  SendMessageToProcessAtMicroseconds(p1, 30000, 303);
  SendMessageToProcessAtMicroseconds(p1, 10000, 101);

  // Tick 1: t goes to 10000. Since events are sorted, the t=10000 event must
  // trigger first!
  TimerHandler();
  ASSERT(GetCurrentTimestampInMicroseconds(), (size_t)10000);
  ASSERT(p1->messages_queued, (size_t)1);

  Message* msg = GetNextQueuedMessage(p1);
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)101);

  // Tick 2: t goes to 20000. No events.
  TimerHandler();
  ASSERT(p1->messages_queued, (size_t)0);

  // Tick 3: t goes to 30000. The t=30000 event triggers!
  TimerHandler();
  ASSERT(p1->messages_queued, (size_t)1);

  msg = GetNextQueuedMessage(p1);
  ASSERT(msg != nullptr, true);
  ASSERT(msg->message_id, (size_t)303);
}

TEST(TimerCancellationTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeTimer();

  Process* p1 = CreateTestProcess("Process1");
  ASSERT(p1 != nullptr, true);

  // Schedule an event at t = 30000
  SendMessageToProcessAtMicroseconds(p1, 30000, 505);

  // Cancel all events for p1
  CancelAllTimerEventsForProcess(p1);

  // Tick 1, 2, 3: t goes to 30000
  TimerHandler();
  TimerHandler();
  TimerHandler();

  ASSERT(GetCurrentTimestampInMicroseconds(), (size_t)30000);
  // Verify no notification was received
  ASSERT(p1->messages_queued, (size_t)0);
}
