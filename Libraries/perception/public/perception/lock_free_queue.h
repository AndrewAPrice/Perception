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

#include <atomic>
#include <cstddef>

namespace perception {

// Lock-free Single-Producer Single-Consumer Queue of pointers.
template <typename T, size_t Size>
class LockFreeQueue {
 public:
  LockFreeQueue() : head_(0), tail_(0) {
    for (size_t i = 0; i < Size; ++i) {
      slots_[i].store(nullptr, std::memory_order_relaxed);
    }
  }

  bool Push(T* item) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t head = head_.load(std::memory_order_acquire);
    if (tail - head >= Size) return false;
    slots_[tail % Size].store(item, std::memory_order_release);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  T* Pop() {
    size_t head = head_.load(std::memory_order_relaxed);
    size_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail) return nullptr;
    T* item = slots_[head % Size].load(std::memory_order_acquire);
    if (item == nullptr) return nullptr;
    slots_[head % Size].store(nullptr, std::memory_order_relaxed);
    head_.store(head + 1, std::memory_order_release);
    return item;
  }

  T* Peek() {
    size_t head = head_.load(std::memory_order_relaxed);
    size_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail) return nullptr;
    return slots_[head % Size].load(std::memory_order_acquire);
  }

 private:
  std::atomic<size_t> head_;
  std::atomic<size_t> tail_;
  std::atomic<T*> slots_[Size];
};

}  // namespace perception
