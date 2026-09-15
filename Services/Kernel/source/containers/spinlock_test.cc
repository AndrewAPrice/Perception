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

#include "containers/spinlock.h"
#include "testing.h"

using containers::InterruptSafeSpinlock;
using containers::InterruptSafeSpinlockGuard;
using containers::Spinlock;
using containers::SpinlockGuard;

TEST(SpinlockAcquireRelease) {
  Spinlock lock;
  ASSERT(lock.TryAcquire(), true);
  ASSERT(lock.TryAcquire(), false);
  lock.Release();
  ASSERT(lock.TryAcquire(), true);
  lock.Release();
}

TEST(SpinlockGuardBasic) {
  Spinlock lock;
  {
    SpinlockGuard guard(lock);
    ASSERT(lock.TryAcquire(), false);
  }
  ASSERT(lock.TryAcquire(), true);
  lock.Release();
}

TEST(InterruptSafeSpinlockBasic) {
  InterruptSafeSpinlock lock;
  {
    InterruptSafeSpinlockGuard guard(lock);
  }
  size_t rflags = 0;
  lock.Acquire(rflags);
  lock.Release(rflags);
}
