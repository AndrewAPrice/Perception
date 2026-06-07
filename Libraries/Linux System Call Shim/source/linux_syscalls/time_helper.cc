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

#include "linux_syscalls/time_helper.h"

#include <atomic>
#include <cstring>

#include "perception/messages.h"
#include "perception/time.h"

namespace perception {
namespace linux_syscalls {
namespace {

std::atomic<uint64> utc_offset = 0;
std::atomic<double> tsc_multiplier = 0.0;
MessageId update_message_id = 0;

void TimeUpdateHandler(ProcessId sender, const MessageData& message) {
  if (sender != 0) return;  // Only accept messages from kernel.

  uint64 offset = message.param1;
  uint64 tsc_ticks_per_microsecond = message.param2;
  double multiplier = 1.0 / (double)tsc_ticks_per_microsecond;

  utc_offset.store(offset, std::memory_order_relaxed);
  tsc_multiplier.store(multiplier, std::memory_order_relaxed);
}

void InitializeTimeHelper() {
  uint64 offset = 0;
  double multiplier = 1.0;

  // Get current time info from kernel.
  perception::GetTimeInfo(offset, multiplier);

  utc_offset.store(offset, std::memory_order_relaxed);
  tsc_multiplier.store(multiplier, std::memory_order_relaxed);

  // Register for when the time info from the kernel changes.
  update_message_id = perception::GenerateUniqueMessageId();
  perception::RegisterMessageHandler(update_message_id, TimeUpdateHandler);
  perception::RegisterMessageForWhenTimeInfoChanges(update_message_id);
}

}  // namespace

uint64 GetUtcTimeInMicroseconds() {
  static bool init = ([]() {
    InitializeTimeHelper();
    return true;
  })();

  uint64 tsc = perception::GetClockCyclesSinceBoot();
  double mult = tsc_multiplier.load(std::memory_order_relaxed);
  uint64 offset = utc_offset.load(std::memory_order_relaxed);

  return offset + (uint64)((double)tsc * mult);
}

}  // namespace linux_syscalls
}  // namespace perception
