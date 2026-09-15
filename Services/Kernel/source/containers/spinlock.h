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

#include "hardware/current_core.h"
#include "hardware/tlb_shootdown.h"
#include "types.h"

namespace containers {

// CPU pause instruction helper for busy-spin loops.
inline void CpuPause() {
#if defined(__x86_64__) || defined(_M_X64)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
  asm volatile("yield");
#endif
}

// RFLAGS Interrupt Flag mask bit.
constexpr size_t kRflagsInterruptFlag = 1 << 9;

// Hardware ticket spinlock guaranteeing FIFO fair ordering across CPU cores.
class Spinlock {
 public:
  constexpr Spinlock() : ticket_(0), now_serving_(0) {}

  // Acquires the lock, spinning until available.
  void Acquire() {
    uint32 my_ticket = __atomic_fetch_add(&ticket_, 1, __ATOMIC_RELAXED);
    while (__atomic_load_n(&now_serving_, __ATOMIC_ACQUIRE) != my_ticket) {
      // Interrupts are disabled while a core waits here, so it cannot take a
      // TLB shootdown IPI. Servicing shootdowns from the spin loop keeps a core
      // that is blocked on a lock held by the shootdown's sender from
      // deadlocking both of them.
      ::hardware::PollTlbShootdown();
      CpuPause();
    }
  }

  // Attempts to acquire the lock without blocking. Returns true if acquired.
  bool TryAcquire() {
    uint32 current = __atomic_load_n(&now_serving_, __ATOMIC_ACQUIRE);
    uint32 expected = current;
    return __atomic_compare_exchange_n(&ticket_, &expected, current + 1, false,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
  }

  // Releases the lock.
  void Release() {
    uint32 current = __atomic_load_n(&now_serving_, __ATOMIC_RELAXED);
    __atomic_store_n(&now_serving_, current + 1, __ATOMIC_RELEASE);
  }

  // Resets the lock to the unheld state. Only safe when no core holds it.
  void Initialize() {
    __atomic_store_n(&ticket_, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&now_serving_, 0, __ATOMIC_RELEASE);
  }

  uint32 ticket() const { return __atomic_load_n(&ticket_, __ATOMIC_RELAXED); }
  uint32 now_serving() const {
    return __atomic_load_n(&now_serving_, __ATOMIC_RELAXED);
  }

 private:
  uint32 ticket_;
  uint32 now_serving_;
};

// RAII guard for basic spinlock.
class SpinlockGuard {
 public:
  explicit SpinlockGuard(Spinlock& lock) : lock_(lock) { lock_.Acquire(); }

  ~SpinlockGuard() { lock_.Release(); }

  SpinlockGuard(const SpinlockGuard&) = delete;
  SpinlockGuard& operator=(const SpinlockGuard&) = delete;

 private:
  Spinlock& lock_;
};

// Spinlock that disables local interrupts while held to prevent interrupt deadlock.
class InterruptSafeSpinlock {
 public:
  constexpr InterruptSafeSpinlock() : lock_() {}

  // Acquires the lock and saves the previous interrupt state into rflags.
  void Acquire(size_t& rflags) {
#ifndef TEST
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags) :: "memory");
#else
    rflags = 0;
#endif
    lock_.Acquire();
  }

  // Releases the lock and restores previous interrupt state from rflags.
  void Release(size_t rflags) {
    lock_.Release();
#ifndef TEST
    if (rflags & kRflagsInterruptFlag) asm volatile("sti" ::: "memory");
#else
    (void)rflags;
#endif
  }

 private:
  Spinlock lock_;
};

// RAII guard for interrupt-safe spinlock.
class InterruptSafeSpinlockGuard {
 public:
  explicit InterruptSafeSpinlockGuard(InterruptSafeSpinlock& lock)
      : lock_(lock), rflags_(0) {
    lock_.Acquire(rflags_);
  }

  ~InterruptSafeSpinlockGuard() { lock_.Release(rflags_); }

  InterruptSafeSpinlockGuard(const InterruptSafeSpinlockGuard&) = delete;
  InterruptSafeSpinlockGuard& operator=(const InterruptSafeSpinlockGuard&) = delete;

 private:
  InterruptSafeSpinlock& lock_;
  size_t rflags_;
};

// RAII guard to disable local interrupts on the executing core.
class ScopedInterruptDisabler {
 public:
  ScopedInterruptDisabler() : rflags_(0) {
#ifndef TEST
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags_) :: "memory");
#endif
  }

  ~ScopedInterruptDisabler() {
#ifndef TEST
    if (rflags_ & kRflagsInterruptFlag) asm volatile("sti" ::: "memory");
#endif
  }

  ScopedInterruptDisabler(const ScopedInterruptDisabler&) = delete;
  ScopedInterruptDisabler& operator=(const ScopedInterruptDisabler&) = delete;

 private:
  size_t rflags_;
};

// RAII guard to acquire two InterruptSafeSpinlocks in consistent pointer address order,
// preventing ABBA deadlocks.
class ScopedTwoLocks {
 public:
  ScopedTwoLocks(InterruptSafeSpinlock* a, InterruptSafeSpinlock* b)
      : first_(a), second_(b), single_lock_(false) {
    if (a == nullptr || b == nullptr || a == b) {
      single_lock_ = true;
      first_ = (a != nullptr) ? a : b;
      if (first_ != nullptr) first_->Acquire(rflags1_);
    } else if (reinterpret_cast<size_t>(a) < reinterpret_cast<size_t>(b)) {
      a->Acquire(rflags1_);
      b->Acquire(rflags2_);
    } else {
      b->Acquire(rflags1_);
      a->Acquire(rflags2_);
      first_ = b;
      second_ = a;
    }
  }

  ~ScopedTwoLocks() {
    if (single_lock_) {
      if (first_ != nullptr) first_->Release(rflags1_);
    } else {
      second_->Release(rflags2_);
      first_->Release(rflags1_);
    }
  }

  ScopedTwoLocks(const ScopedTwoLocks&) = delete;
  ScopedTwoLocks& operator=(const ScopedTwoLocks&) = delete;

 private:
  InterruptSafeSpinlock* first_;
  InterruptSafeSpinlock* second_;
  bool single_lock_;
  size_t rflags1_ = 0;
  size_t rflags2_ = 0;
};

// Value of owner_core_ meaning the lock is not currently held by any core.
constexpr int kNoOwnerCore = -1;

// Recursive spinlock that disables local interrupts while held to prevent interrupt deadlock.
class RecursiveInterruptSafeSpinlock {
 public:
  constexpr RecursiveInterruptSafeSpinlock()
      : lock_(), owner_core_(kNoOwnerCore), recursion_count_(0),
        saved_rflags_(0) {}

  // Initializes the spinlock state.
  void Initialize() {
    lock_.Initialize();
    __atomic_store_n(&recursion_count_, 0, __ATOMIC_RELAXED);
    saved_rflags_ = 0;
    __atomic_store_n(&owner_core_, kNoOwnerCore, __ATOMIC_RELEASE);
  }

  // Acquires the lock, supporting recursion on the same CPU core.
  void Acquire() {
    size_t rflags = 0;
#ifndef TEST
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags) :: "memory");
#endif
    // Also polled here rather than only in Spinlock::Acquire below, because the
    // recursion fast path never reaches it. A loop that re-enters a lock it
    // already holds would otherwise stall a core waiting for its acknowledgement
    // for the whole duration of the loop.
    ::hardware::PollTlbShootdown();

    int core_id = static_cast<int>(::hardware::GetCurrentCoreId());

    // Only the owning core can observe its own ID here, and it cannot be
    // concurrently releasing, so the recursion count is stable in this branch.
    if (__atomic_load_n(&owner_core_, __ATOMIC_ACQUIRE) == core_id) {
      __atomic_fetch_add(&recursion_count_, 1, __ATOMIC_RELAXED);
      return;
    }

    lock_.Acquire();
    __atomic_store_n(&recursion_count_, 1, __ATOMIC_RELAXED);
    saved_rflags_ = rflags;
    // Published last so no other core can observe ownership before the count
    // and saved flags are valid.
    __atomic_store_n(&owner_core_, core_id, __ATOMIC_RELEASE);
  }

  // Releases one level of recursion. Releases the spinlock when recursion reaches zero.
  void Release() {
    int core_id = static_cast<int>(::hardware::GetCurrentCoreId());

    // Releasing a lock this core does not hold is a kernel bug. Trapping
    // surfaces it as a diagnosable fault instead of silently leaving the lock
    // held forever, which would wedge every core that touches it later.
    if (__atomic_load_n(&owner_core_, __ATOMIC_ACQUIRE) != core_id)
      __builtin_trap();

    if (__atomic_sub_fetch(&recursion_count_, 1, __ATOMIC_RELAXED) > 0) return;

    size_t rflags = saved_rflags_;
    __atomic_store_n(&owner_core_, kNoOwnerCore, __ATOMIC_RELEASE);
    lock_.Release();
#ifndef TEST
    if (rflags & kRflagsInterruptFlag) asm volatile("sti" ::: "memory");
#else
    (void)rflags;
#endif
  }

  int owner_core() const {
    return __atomic_load_n(&owner_core_, __ATOMIC_RELAXED);
  }
  int recursion_count() const {
    return __atomic_load_n(&recursion_count_, __ATOMIC_RELAXED);
  }
  const Spinlock& raw_lock() const { return lock_; }

 private:
  Spinlock lock_;
  int owner_core_;
  int recursion_count_;
  size_t saved_rflags_;
};

// RAII guard for recursive interrupt-safe spinlock.
class RecursiveInterruptSafeSpinlockGuard {
 public:
  explicit RecursiveInterruptSafeSpinlockGuard(
      RecursiveInterruptSafeSpinlock& lock)
      : lock_(lock) {
    lock_.Acquire();
  }

  ~RecursiveInterruptSafeSpinlockGuard() { lock_.Release(); }

  RecursiveInterruptSafeSpinlockGuard(
      const RecursiveInterruptSafeSpinlockGuard&) = delete;
  RecursiveInterruptSafeSpinlockGuard& operator=(
      const RecursiveInterruptSafeSpinlockGuard&) = delete;

 private:
  RecursiveInterruptSafeSpinlock& lock_;
};

}  // namespace containers


