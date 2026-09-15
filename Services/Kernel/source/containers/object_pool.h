// Copyright 2024 Google LLC
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

#ifndef TEST
#include "memory/heap_allocator.h"
#include "memory/memory.h"
#else
#include <stdlib.h>
#include <cstring>
#include <new>
#endif
#include "containers/spinlock.h"
#include "hardware/current_core.h"

namespace containers {

// Maximum number of cached objects held in a CPU core's private pool.
constexpr size_t kMaxLocalCachedObjects = 32;

// Number of objects transferred between local and global pools during batch
// operations.
constexpr size_t kLocalPoolBatchSize = 16;

class ObjectPoolHelper;

// Matches pooled types that carry their own `is_static` flag. Instances of such
// types may be statically allocated and then handed to the pool, so the pool
// must remember not to free them. Detecting the flag automatically means a
// pooled type opts in simply by declaring `bool is_static`, with no risk of a
// translation unit missing an out-of-line customization and silently treating
// static objects as heap allocated.
template <class T>
concept HasStaticFlag = requires(T& object) { object.is_static = true; };

// An item on the object pool.
struct ObjectPoolItem {
  // The next item on the object pool.
  ObjectPoolItem* next;

  // Whether this object was statically allocated, and therefore shouldn't be freed.
  bool is_static;
};

// An object pool.
// https://en.wikipedia.org/wiki/Object_pool_pattern
//
// Each core keeps a small private cache that is accessed without locks or
// atomics. This is safe only because the kernel never runs with interrupts
// enabled, so nothing else can run on this core between reading the core ID and
// finishing with that core's cache. Other cores only ever touch their own
// cache, and the shared free list below is guarded by `lock_`.
template <class T>
class ObjectPool {
  friend ObjectPoolHelper;

 public:
  // Returns an object, preferably from the local per-CPU cache or global pool.
  // Returns nullptr if the system is out of memory.
  static T* Allocate() {
    bool is_static = false;
    T* obj = nullptr;

    size_t core = CurrentCore();

    // Fast path: pop from this core's local cache without cross-core locks.
    // ScopedInterruptDisabler ensures local interrupt handlers cannot mutate the
    // cache concurrently.
    {
      ScopedInterruptDisabler disabler;
      if (local_cache_[core] != nullptr) {
        ObjectPoolItem* item = local_cache_[core];
        local_cache_[core] = item->next;
        local_count_[core]--;
        is_static = item->is_static;
        obj = reinterpret_cast<T*>(item);
      }
    }
    if (obj != nullptr) return ConstructObject(obj, is_static);

    // Slow path: refill a batch from the global pool under spinlock.
    {
      InterruptSafeSpinlockGuard guard(lock_);
      if (next_item_ != nullptr) {
        ObjectPoolItem* item = next_item_;
        next_item_ = item->next;
        is_static = item->is_static;
        obj = reinterpret_cast<T*>(item);

        size_t batch = 0;
        while (next_item_ != nullptr && batch < kLocalPoolBatchSize) {
          ObjectPoolItem* next_batch_item = next_item_;
          next_item_ = next_batch_item->next;
          next_batch_item->next = local_cache_[core];
          local_cache_[core] = next_batch_item;
          batch++;
        }
        local_count_[core] += batch;
      }
    }

    if (obj == nullptr) {
      obj = reinterpret_cast<T*>(malloc(sizeof(T)));
      if (obj == nullptr) return nullptr;
      memset(reinterpret_cast<char*>(obj), 0, sizeof(T));
    }

    return ConstructObject(obj, is_static);
  }

  // Releases an object back to the pool.
  static void Release(T* obj) {
    if (obj == nullptr) return;
    bool is_static = IsObjectStatic(obj);
    obj->~T();
    auto item = reinterpret_cast<ObjectPoolItem*>(obj);
    item->is_static = is_static;

    size_t core = CurrentCore();

    // Fast path: push to local core cache if within capacity.
    // ScopedInterruptDisabler ensures local interrupt handlers cannot mutate the
    // cache concurrently.
    {
      ScopedInterruptDisabler disabler;
      if (local_count_[core] < kMaxLocalCachedObjects) {
        item->next = local_cache_[core];
        local_cache_[core] = item;
        local_count_[core]++;
        return;
      }
    }

    // Slow path: flush half the local cache to the global pool under spinlock.
    {
      InterruptSafeSpinlockGuard guard(lock_);
      for (size_t i = 0;
           i < kLocalPoolBatchSize && local_cache_[core] != nullptr; i++) {
        ObjectPoolItem* to_move = local_cache_[core];
        local_cache_[core] = to_move->next;
        local_count_[core]--;
        to_move->next = next_item_;
        next_item_ = to_move;
      }

      item->next = next_item_;
      next_item_ = item;
    }
  }

 private:
  // The next item on the global object pool.
  static ObjectPoolItem* next_item_;

  // Spinlock protecting this pool's global free list.
  static InterruptSafeSpinlock lock_;

  // Per-CPU local caches for lockless fast-path allocation and release.
  static ObjectPoolItem* local_cache_[::hardware::kMaxCores];
  static size_t local_count_[::hardware::kMaxCores];

  // Returns the index into the per-core arrays for the calling core, clamped so
  // that a bogus core ID can never index out of bounds.
  static size_t CurrentCore() {
    size_t core = ::hardware::GetCurrentCoreId();
    return core >= static_cast<size_t>(::hardware::kMaxCores) ? 0 : core;
  }

  // Returns whether an object was statically allocated and so must not be
  // freed.
  static bool IsObjectStatic(T* obj) {
    if constexpr (HasStaticFlag<T>) {
      return obj->is_static;
    } else {
      return false;
    }
  }

  // Constructs an object in place, preserving whether it was statically
  // allocated across the construction.
  static T* ConstructObject(T* obj, bool is_static) {
    T* constructed = new (obj) T();
    if constexpr (HasStaticFlag<T>) constructed->is_static = is_static;
    return constructed;
  }

  // Frees all non-static objects held in the global pool, along with those
  // cached by the calling core. The other cores' caches are deliberately left
  // alone: they are read and written without locks, so they may only be touched
  // by the core that owns them.
  static void FreeObjectsInPool() {
    ObjectPoolItem* to_free = nullptr;
    {
      InterruptSafeSpinlockGuard guard(lock_);
      size_t core = CurrentCore();
      while (local_cache_[core] != nullptr) {
        ObjectPoolItem* item = local_cache_[core];
        local_cache_[core] = item->next;
        item->next = next_item_;
        next_item_ = item;
      }
      local_count_[core] = 0;

      ObjectPoolItem* prev = nullptr;
      ObjectPoolItem* curr = next_item_;
      while (curr != nullptr) {
        if (curr->is_static) {
          prev = curr;
          curr = curr->next;
        } else {
          ObjectPoolItem* next = curr->next;
          if (prev == nullptr) {
            next_item_ = next;
          } else {
            prev->next = next;
          }
          curr->next = to_free;
          to_free = curr;
          curr = next;
        }
      }
    }
    while (to_free != nullptr) {
      ObjectPoolItem* next = to_free->next;
      free(to_free);
      to_free = next;
    }
  }
};

template <class T>
ObjectPoolItem* ObjectPool<T>::next_item_ = nullptr;

template <class T>
InterruptSafeSpinlock ObjectPool<T>::lock_;

template <class T>
ObjectPoolItem* ObjectPool<T>::local_cache_[::hardware::kMaxCores] = {nullptr};

template <class T>
size_t ObjectPool<T>::local_count_[::hardware::kMaxCores] = {0};

}  // namespace containers
