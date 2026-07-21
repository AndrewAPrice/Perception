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
#include "heap_allocator.h"
#include "memory.h"
#else
#include <stdlib.h>
#include <cstring>
#include <new>
#endif

class ObjectPoolHelper;

// An item on the object pool.
struct ObjectPoolItem {
  // The next item on the object pool.
  ObjectPoolItem* next;

  // Whether this object was statically allocated, and therefore shouldn't be freed.
  bool is_static;
};

// An object pool.
// https://en.wikipedia.org/wiki/Object_pool_pattern
template <class T>
class ObjectPool {
  friend ObjectPoolHelper;

 public:
  // Helper to check if an object is static before destruction.
  static bool IsObjectStatic(T* obj) { return false; }

  // Helper to construct an object with static status.
  static T* ConstructObject(T* obj, bool is_static) {
    return new (obj) T();
  }

  // Returns an object, preferably from the pool.
  static T* Allocate() {
#ifndef TEST
    BEGIN_NO_INTERRUPTS();
#endif
    bool is_static = false;
    T* obj = (T*)next_item_;
    if (obj == nullptr) {
      obj = (T*)malloc(sizeof(T));
      memset((char*)obj, 0, sizeof(T));
    } else {
      is_static = next_item_->is_static;
      next_item_ = next_item_->next;
    }
#ifndef TEST
    END_NO_INTERRUPTS();
#endif

    return ConstructObject(obj, is_static);
  }

  // Releases an object back to the pool.
  static void Release(T* obj) {
    bool is_static = IsObjectStatic(obj);
    obj->~T();
#ifndef TEST
    BEGIN_NO_INTERRUPTS();
#endif
    auto item = (ObjectPoolItem*)obj;
    item->is_static = is_static;
    item->next = next_item_;
    next_item_ = item;
#ifndef TEST
    END_NO_INTERRUPTS();
#endif
  }

 private:
  // The next item on the object pool.
  static ObjectPoolItem* next_item_;

  // Frees all non-static objects in the pool.
  static void FreeObjectsInPool() {
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
        free(curr);
        curr = next;
      }
    }
  }
};

template <class T>
ObjectPoolItem* ObjectPool<T>::next_item_ = 0;
