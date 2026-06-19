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

#include <atomic>
#include <cstring>
#include "perception/memory.h"
#include "tlsf.h"

namespace {

std::atomic_flag liballoc_spinlock = ATOMIC_FLAG_INIT;

void liballoc_lock() {
  while (liballoc_spinlock.test_and_set(std::memory_order_acquire)) {
    // Spin
  }
}

void liballoc_unlock() {
  liballoc_spinlock.clear(std::memory_order_release);
}

tlsf_t g_tlsf = nullptr;
constexpr size_t kPageSize = 4096;
constexpr size_t kPagesPerChunk = 256; // Allocate 1MB at a time

bool ExpandHeap(size_t minimum_size) {
  size_t pages = kPagesPerChunk;
  size_t needed_bytes = minimum_size * 2 + tlsf_pool_overhead() + tlsf_alloc_overhead() + 128;
  if (g_tlsf == nullptr) {
    needed_bytes += tlsf_size();
  }
  size_t needed_pages = (needed_bytes + kPageSize - 1) / kPageSize;
  if (needed_pages > pages) {
    pages = needed_pages;
  }

  void* mem = ::perception::AllocateMemoryPages(pages);
  if (mem == nullptr) {
    return false;
  }
  if (g_tlsf == nullptr) {
    g_tlsf = tlsf_create_with_pool(mem, pages * kPageSize - 32);
    return g_tlsf != nullptr;
  } else {
    pool_t pool = tlsf_add_pool(
        g_tlsf, mem, pages * kPageSize - 32);
    return pool != nullptr;
  }
}

}  // namespace

extern "C" {

void* malloc(size_t size) {
  if (size == 0) return nullptr;
  liballoc_lock();
  if (g_tlsf == nullptr && !ExpandHeap(size)) {
    liballoc_unlock();
    return nullptr;
  }
  void* ptr = tlsf_malloc(g_tlsf, size);
  if (ptr == nullptr) {
    // Attempt to expand the heap and retry
    if (ExpandHeap(size)) {
      ptr = tlsf_malloc(g_tlsf, size);
    }
  }
  liballoc_unlock();
  return ptr;
}

void free(void* ptr) {
  if (ptr == nullptr) return;
  liballoc_lock();
  if (g_tlsf != nullptr) {
    tlsf_free(g_tlsf, ptr);
  }
  liballoc_unlock();
}

void* realloc(void* ptr, size_t size) {
  if (ptr == nullptr) return malloc(size);
  if (size == 0) {
    free(ptr);
    return nullptr;
  }
  liballoc_lock();
  void* new_ptr = tlsf_realloc(g_tlsf, ptr, size);
  if (new_ptr == nullptr) {
    if (ExpandHeap(size)) {
      new_ptr = tlsf_realloc(g_tlsf, ptr, size);
    }
  }
  liballoc_unlock();
  return new_ptr;
}

void* calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  void* ptr = malloc(total);
  if (ptr != nullptr) {
    std::memset(ptr, 0, total);
  }
  return ptr;
}

void* aligned_alloc(size_t alignment, size_t size) {
  if (size == 0) return nullptr;
  liballoc_lock();
  if (g_tlsf == nullptr && !ExpandHeap(size)) {
    liballoc_unlock();
    return nullptr;
  }
  void* ptr = tlsf_memalign(g_tlsf, alignment, size);
  if (ptr == nullptr) {
    if (ExpandHeap(size)) {
      ptr = tlsf_memalign(g_tlsf, alignment, size);
    }
  }
  liballoc_unlock();
  return ptr;
}

void* memalign(size_t alignment, size_t size) {
  return aligned_alloc(alignment, size);
}

size_t malloc_usable_size(void* ptr) {
  if (ptr == nullptr) return 0;
  // tlsf_block_size returns the actual usable size of the block.
  // We do not lock here because the block is already allocated
  // and owned by this thread, so its metadata won't change concurrently.
  return tlsf_block_size(ptr);
}

}  // extern "C"
