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

#include "memory/heap_allocator.h"

#include "../../../third_party/Libraries/tlsf/public/tlsf.h"
#include "memory/memory.h"
#include "memory/physical_allocator.h"
#include "containers/spinlock.h"
#include "output/text_terminal.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"

using containers::InterruptSafeSpinlock;
using containers::InterruptSafeSpinlockGuard;
using memory::kPageSize;
using memory::KernelAddressSpace;

namespace {

tlsf_t g_kernel_tlsf = nullptr;
InterruptSafeSpinlock g_heap_spinlock;
// Default number of pages allocated when expanding heap chunks (1MB).
constexpr size_t kPagesPerChunk = 256;

// Largest request that can be sized without overflowing the doubling and
// overhead arithmetic in CalculateNeededPages.
constexpr size_t kMaxAllocationSize = ~static_cast<size_t>(0) / 4;

// Returns 0 if `minimum_size` is too large to ever be satisfied.
size_t CalculateNeededPages(size_t minimum_size) {
  if (minimum_size > kMaxAllocationSize) return 0;
  size_t pages = kPagesPerChunk;
  size_t needed_bytes =
      minimum_size * 2 + tlsf_pool_overhead() + tlsf_alloc_overhead() + 128;
  if (g_kernel_tlsf == nullptr) needed_bytes += tlsf_size();
  size_t needed_pages = (needed_bytes + kPageSize - 1) / kPageSize;
  if (needed_pages > pages) pages = needed_pages;
  return pages;
}

bool AddMemoryToHeap(void* mem, size_t pages) {
  if (g_kernel_tlsf == nullptr) {
    g_kernel_tlsf = tlsf_create_with_pool(mem, pages * kPageSize - 32);
    return g_kernel_tlsf != nullptr;
  } else {
    pool_t pool = tlsf_add_pool(g_kernel_tlsf, mem, pages * kPageSize - 32);
    return pool != nullptr;
  }
}

}  // namespace

extern "C" {

void* malloc(size_t size) {
  if (size == 0) return nullptr;
  {
    InterruptSafeSpinlockGuard guard(g_heap_spinlock);
    if (g_kernel_tlsf != nullptr) {
      void* ptr = tlsf_malloc(g_kernel_tlsf, size);
      if (ptr != nullptr) return ptr;
    }
  }

  // Allocate pages from KernelAddressSpace without holding g_heap_spinlock
  // to prevent lock inversion with KernelAddressSpace().lock_.
  size_t pages = CalculateNeededPages(size);
  if (pages == 0) return nullptr;
  size_t addr = KernelAddressSpace().AllocatePages(pages);
  if (addr == 0 || addr == kOutOfMemory) return nullptr;

  {
    InterruptSafeSpinlockGuard guard(g_heap_spinlock);
    if (AddMemoryToHeap((void*)addr, pages))
      return tlsf_malloc(g_kernel_tlsf, size);
  }

  // The pages could not be handed to the heap, so release them outside of
  // g_heap_spinlock rather than leaking them.
  KernelAddressSpace().FreePages(addr, pages);
  return nullptr;
}

void free(void* ptr) {
  if (ptr == nullptr) return;
  InterruptSafeSpinlockGuard guard(g_heap_spinlock);
  if (g_kernel_tlsf != nullptr) tlsf_free(g_kernel_tlsf, ptr);
}

void* realloc(void* ptr, size_t size) {
  if (ptr == nullptr) return malloc(size);
  if (size == 0) {
    free(ptr);
    return nullptr;
  }
  {
    InterruptSafeSpinlockGuard guard(g_heap_spinlock);
    if (g_kernel_tlsf != nullptr) {
      void* new_ptr = tlsf_realloc(g_kernel_tlsf, ptr, size);
      if (new_ptr != nullptr) return new_ptr;
    }
  }

  // Allocate pages without holding g_heap_spinlock.
  size_t pages = CalculateNeededPages(size);
  if (pages == 0) return nullptr;
  size_t addr = KernelAddressSpace().AllocatePages(pages);
  if (addr == 0 || addr == kOutOfMemory) return nullptr;

  {
    InterruptSafeSpinlockGuard guard(g_heap_spinlock);
    if (AddMemoryToHeap((void*)addr, pages))
      return tlsf_realloc(g_kernel_tlsf, ptr, size);
  }

  // The pages could not be handed to the heap, so release them outside of
  // g_heap_spinlock rather than leaking them.
  KernelAddressSpace().FreePages(addr, pages);
  return nullptr;
}

void* calloc(size_t nmemb, size_t size) {
  if (nmemb != 0 && size > kMaxAllocationSize / nmemb) return nullptr;
  size_t total = nmemb * size;
  void* ptr = malloc(total);
  if (ptr != nullptr) {
    memset((char*)ptr, 0, total);
  }
  return ptr;
}

}  // extern "C"
