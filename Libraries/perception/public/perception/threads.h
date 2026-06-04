// Copyright 2020 Google LLC
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

#include <optional>
#include "types.h"

namespace perception {

// Used to identify threads.
typedef size_t ThreadId;

enum class ThreadPriority : uint8 {
  // Strictly preemptive priority. Hardware drivers (e.g., keyboard, mouse,
  // disk, network). Preempts all lower-priority threads immediately,
  // preventing them from running.
  InterruptDriver = 0,

  // Strictly preemptive priority. UI compositor, audio servers, Window Manager.
  // Preempts all lower-priority threads immediately, preventing them from
  // running.
  RealtimeService = 1,

  // Temporarily boosted priority of the focused application.
  InteractiveApp = 2,

  // Normal priority.
  Normal = 3,

  // Low priority worker threads, CPU-heavy compiling, and search indexing.
  Background = 4,

  // Only runs if the system is otherwise idle.
  Idle = 5
};

// Sets the priority of the current thread.
bool SetThreadPriority(ThreadPriority priority);

// Sets the priority of a specific thread.
bool SetThreadPriority(ThreadId tid, ThreadPriority priority);

// Creates a thread. The provided (optional) parameter is passed through to the
// newly running thread. It's unsafe for the entry_point function to simply
// return. It should call TerminateThread() when no longer needed.
ThreadId CreateThread(void (*entry_point)(void*), void* param);

// Gets the ID of the currently executing thread.
ThreadId GetThreadId();

// Returns whether the current thread is the primary/main thread.
bool IsPrimaryThread();

// Returns the Thread ID of the primary/main thread.
ThreadId GetPrimaryThreadId();

// Terminates the currently running thread.
void TerminateThread();

// Terminate the thread associaed with the provided thread id.
void TerminateThread(ThreadId tid);

// Sets the address for the thread's segment (FS).
void SetThreadSegment(size_t segment_address);

// Sets the address for the thread's segments (FS and/or GS).
void SetThreadSegments(std::optional<size_t> fs_address,
                       std::optional<size_t> gs_address);

// Set an address (that must be 8-byte aligned) to be cleared on the termination
// of the currently executing thread.
void SetAddressToClearOnThreadTermination(size_t address);

// Yields execution of the current thread.
inline void SleepThisThread() {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 3;
  __asm__ __volatile__("syscall\n" : : "r"(syscall) : "rcx", "r11");
#endif
}

// Wakes up a thread with the given ThreadId.
inline void WakeThread(ThreadId tid) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 10;
  volatile register size_t rax asm("rax") = tid;
  __asm__ __volatile__("syscall\n" : : "r"(syscall), "r"(rax) : "rcx", "r11");
#endif
}

}  // namespace perception
