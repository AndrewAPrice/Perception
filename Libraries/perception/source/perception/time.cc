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

#include "perception/time.h"

#ifndef PERCEPTION
#include <iostream>
#endif
#include <cstring>

#include "perception/messages.h"
#include "perception/scheduler.h"

namespace perception {
namespace {

// Tells the kernel to send us a message in a certain number of microseconds
// from now.
void SendMessageInMicrosecondsFromNow(size_t microseconds, size_t message_id) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 23;
  volatile register size_t microseconds_r asm("rax") = microseconds;
  volatile register size_t message_id_r asm("rbx") = message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall), "r"(microseconds_r),
                       "r"(message_id_r)
                       : "rcx", "r11");
#else
  std::cout << "Implement time.cc:SendMessageAtMicroseconds" << std::endl;
#endif
}

// Tells the kernel to send us a message after a certain number of microseconds
// since the kernel started.
void SendMessageAtMicrosecondsSinceKernelStart(size_t microseconds,
                                               size_t message_id) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 24;
  volatile register size_t microseconds_r asm("rax") = microseconds;
  volatile register size_t message_id_r asm("rbx") = message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall), "r"(microseconds_r),
                       "r"(message_id_r)
                       : "rcx", "r11");
#else
  std::cout << "Implement time.cc:SendMessageAtMicroseconds" << std::endl;
#endif
}

}  // namespace

// Returns the time since the kernel started.
std::chrono::microseconds GetTimeSinceKernelStarted() {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 25;
  volatile register size_t return_val asm("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val)
                       : "r"(syscall)
                       : "rcx", "r11");
  size_t microseconds = return_val;
  return std::chrono::microseconds(microseconds);
#else
  return std::chrono::microseconds(0);
#endif
}

// Returns the number of CPU clock cycles since the processor turned on.
size_t GetClockCyclesSinceBoot() {
#if defined(PERCEPTION) && !defined(TEST)
  unsigned hi, lo;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
#else
  return 0;
#endif
}

// Sleeps the current fiber and returns after the duration has passed.
void SleepForDuration(std::chrono::microseconds time) {
  MessageId message_id = GenerateUniqueMessageId();
  RegisterWakeUpHandler(message_id);
  SendMessageInMicrosecondsFromNow(time.count(), message_id);

  ProcessId pid;
  MessageData message_data;
  while (true) {
    SleepAndGetRawMessage(message_id, pid, message_data);
    if (pid == 0) break;
    // Re-register if it wasn't the kernel message.
    RegisterWakeUpHandler(message_id);
  }
}

// Sleeps the current fiber and returns after the duration since the
// kernel started has passed.
void SleepUntilTimeSinceKernelStarted(std::chrono::microseconds time) {
  MessageId message_id = GenerateUniqueMessageId();
  RegisterWakeUpHandler(message_id);
  SendMessageAtMicrosecondsSinceKernelStart(time.count(), message_id);

  ProcessId pid;
  MessageData message_data;
  while (true) {
    SleepAndGetRawMessage(message_id, pid, message_data);
    if (pid == 0) break;
    // Re-register if it wasn't the kernel message.
    RegisterWakeUpHandler(message_id);
  }
}

// Calls the on_duration function after a duration has passed.
void AfterDuration(std::chrono::microseconds time,
                   std::function<void()> on_duration) {
  MessageId message_id = GenerateUniqueMessageId();
  SendMessageInMicrosecondsFromNow(time.count(), message_id);

    std::cout << "AfterDuration " << message_id << std::endl;
  ::perception::RegisterMessageHandler(
      message_id,
      [message_id, on_duration](ProcessId sender, const MessageData&) {
        if (sender != 0)
          // Not from the kernel.
          return;

        ::perception::UnregisterMessageHandler(message_id);

        on_duration();
      });
}

// Calls the on_duration function after the duration since the kernel
// started has passed.
void AfterTimeSinceKernelStarted(std::chrono::microseconds time,
                                 std::function<void()> at_time) {
  MessageId message_id = GenerateUniqueMessageId();
  SendMessageAtMicrosecondsSinceKernelStart(time.count(), message_id);

    std::cout << "AfterTimeSinceKernelStarted " << message_id << std::endl;
  ::perception::RegisterMessageHandler(
      message_id, [message_id, at_time](ProcessId sender, const MessageData&) {
        if (sender != 0)
          // Not from the kernel.
          return;

        ::perception::UnregisterMessageHandler(message_id);

        at_time();
      });
}

void GetTimeInfo(uint64& offset, double& tsc_multiplier) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 67;
  volatile register size_t offset_r asm("rax");
  volatile register size_t multiplier_r asm("rbx");

  __asm__ __volatile__("syscall\n"
                       : "=r"(offset_r), "=r"(multiplier_r)
                       : "r"(syscall)
                       : "rcx", "r11");
  offset = offset_r;
  uint64 tsc_ticks_per_microsecond = multiplier_r;
  tsc_multiplier = 1.0 / (double)tsc_ticks_per_microsecond;
#else
  offset = 0;
  tsc_multiplier = 1.0;
#endif
}

void SetTimeInfo(uint64 utc_microseconds) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 68;
  volatile register size_t utc_micros_r asm("rax") = utc_microseconds;

  __asm__ __volatile__("syscall\n" ::"r"(syscall), "r"(utc_micros_r)
                       : "rcx", "r11");
#endif
}

void RegisterMessageForWhenTimeInfoChanges(MessageId message_id) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 69;
  volatile register size_t message_id_r asm("rax") = message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall), "r"(message_id_r)
                       : "rcx", "r11");
#endif
}

}  // namespace perception

// Standard C function to get the current time since the system started, in
// seconds.
extern "C" double getTime() {
  return (double)perception::GetTimeSinceKernelStarted().count() / 1000000.0;
}
