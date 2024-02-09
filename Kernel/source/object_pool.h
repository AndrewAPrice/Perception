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

#include "io.h"
#include "liballoc.h"

class ObjectPoolInitializer;

// An item on the object pool.
struct ObjectPoolItem {
  // The next item on the object pool.
  struct ObjectPoolItem* next;
};

// An object pool.
// https://en.wikipedia.org/wiki/Object_pool_pattern
template <class T>
class ObjectPool {
    friend ObjectPoolInitializer;
public:
    // Returns an object, preferably from the pool.
    static T* Allocate() {
        T* obj = (T*)next_item_;
        if (obj == nullptr) {
            obj = (T*)malloc(sizeof(T));
            memset((char *)obj, 0, sizeof(T));
        } else {
            next_item_ = next_item_->next;
        }

        return new (obj) T();
    }

    // Releases an object back to the pool.
    static void Release(T* obj) {
        obj->~T();
        auto item = (struct ObjectPoolItem*)obj;
        item->next = next_item_;
        next_item_ = item;
    }

    // Frees all the objects in the pool.
    static void FreeObjectsInPool() {
        while (next_item_ != nullptr) {
            auto next = next_item_->next;
            free(next_item_);
            next_item_ = next;
        }
    }

private:
    // The next item on the object pool.
    static ObjectPoolItem *next_item_;
};


template <class T> ObjectPoolItem *ObjectPool<T>::next_item_;

