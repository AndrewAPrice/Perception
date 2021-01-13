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

#include "types.h"

namespace perception {

// Used to identify threads.
typedef size_t ThreadId;

// Creates a thread. The provided (optional) parameter is passed through to the newly running thread.
// It's unsafe for the entry_point function to simply return. It should call TerminateThread() when
// no longer needed.
ThreadId CreateThread(void (*entry_point)(void *), void* param);

// Gets the ID of the currently executing thread.
ThreadId GetThreadId();

// Terminates the currently running thread.
void TerminateThread();

// Terminate the thread associaed with the provided thread id.
void TerminateThread(ThreadId tid);

// Yield's control of the currently running thread. This does not put the thread to sleep, but rather
// passes control to the next thread.
void Yield();

// Sets the address for the thread's segment (FS).
void SetThreadSegment(size_t segment_address);

// Set an address (that must be 8-byte aligned) to be cleared on the termination of the currently
// executing thread.
void SetAddressToClearOnThreadTermination(size_t address);

}
