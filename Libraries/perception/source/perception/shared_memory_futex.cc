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

#include "perception/shared_memory_futex.h"

#include "perception/messages.h"

namespace perception {
namespace {

// Futex state when unlocked.
constexpr uint32 kUnlocked = 0;

// Futex state when locked with no waiting fibers or processes.
constexpr uint32 kLockedNoWaiters = 1;

// Futex state when locked with one or more waiting fibers or processes.
constexpr uint32 kLockedWithWaiters = 2;

// Number of spin iterations before falling back to a shared memory event wait.
constexpr int kSpinIterations = 32;

void CpuPause() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield");
#endif
}

}  // namespace

void SharedMemoryFutex::Lock(SharedMemory& shared_memory, size_t offset,
                             std::atomic<uint32>& state) {
  uint32 expected = kUnlocked;
  if (state.compare_exchange_strong(expected, kLockedNoWaiters,
                                    std::memory_order_acquire))
    return;

  for (int i = 0; i < kSpinIterations &&
                  state.load(std::memory_order_relaxed) == kLockedNoWaiters;
       ++i) {
    CpuPause();
  }

  expected = kUnlocked;
  if (state.compare_exchange_strong(expected, kLockedNoWaiters,
                                    std::memory_order_acquire))
    return;

  while (true) {
    if (state.exchange(kLockedWithWaiters, std::memory_order_acquire) ==
        kUnlocked)
      return;

    MessageId msg_id = GenerateUniqueMessageId();
    RegisterWakeUpHandler(msg_id);
    shared_memory.RegisterEvent(offset, msg_id);

    if (state.exchange(kLockedWithWaiters, std::memory_order_acquire) ==
        kUnlocked) {
      shared_memory.UnregisterEvent(offset);
      UnregisterMessageHandler(msg_id);
      return;
    }

    ProcessId sender = 0;
    MessageData message_data = {};
    SleepAndGetRawMessage(msg_id, sender, message_data);
    shared_memory.UnregisterEvent(offset);
  }
}

void SharedMemoryFutex::Unlock(SharedMemory& shared_memory, size_t offset,
                               std::atomic<uint32>& state) {
  if (state.exchange(kUnlocked, std::memory_order_release) == kLockedNoWaiters)
    return;
  shared_memory.TriggerEvent(offset);
}

}  // namespace perception
