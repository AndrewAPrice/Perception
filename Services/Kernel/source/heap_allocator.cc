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

#include "heap_allocator.h"

#include "../../../third_party/Libraries/tlsf/public/tlsf.h"
#include "memory.h"
#include "text_terminal.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

namespace {

tlsf_t g_kernel_tlsf = nullptr;
constexpr size_t kPageSize = 4096;
constexpr size_t kPagesPerChunk = 256;  // 1MB

bool ExpandKernelHeap(size_t minimum_size) {
  size_t pages = kPagesPerChunk;
  size_t needed_bytes =
      minimum_size * 2 + tlsf_pool_overhead() + tlsf_alloc_overhead() + 128;
  if (g_kernel_tlsf == nullptr) {
    needed_bytes += tlsf_size();
  }
  size_t needed_pages = (needed_bytes + kPageSize - 1) / kPageSize;
  if (needed_pages > pages) {
    pages = needed_pages;
  }

  size_t addr = KernelAddressSpace().AllocatePages(pages);
  if (addr == 0 || addr == OUT_OF_MEMORY) return false;
  void* mem = (void*)addr;
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
  BEGIN_NO_INTERRUPTS();
  if (g_kernel_tlsf == nullptr && !ExpandKernelHeap(size)) {
    END_NO_INTERRUPTS();
    return nullptr;
  }
  void* ptr = tlsf_malloc(g_kernel_tlsf, size);
  if (ptr == nullptr) {
    if (ExpandKernelHeap(size)) {
      ptr = tlsf_malloc(g_kernel_tlsf, size);
    }
  }
  END_NO_INTERRUPTS();
  return ptr;
}

void free(void* ptr) {
  if (ptr == nullptr) return;
  BEGIN_NO_INTERRUPTS();
  if (g_kernel_tlsf != nullptr) {
    tlsf_free(g_kernel_tlsf, ptr);
  }
  END_NO_INTERRUPTS();
}

void* realloc(void* ptr, size_t size) {
  if (ptr == nullptr) return malloc(size);
  if (size == 0) {
    free(ptr);
    return nullptr;
  }
  BEGIN_NO_INTERRUPTS();
  void* new_ptr = tlsf_realloc(g_kernel_tlsf, ptr, size);
  if (new_ptr == nullptr) {
    if (ExpandKernelHeap(size)) {
      new_ptr = tlsf_realloc(g_kernel_tlsf, ptr, size);
    }
  }
  END_NO_INTERRUPTS();
  return new_ptr;
}

void* calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  void* ptr = malloc(total);
  if (ptr != nullptr) {
    memset((char*)ptr, 0, total);
  }
  return ptr;
}

void kernel_debug_print(const char* message) { print << message; }

void kernel_debug_print_hex(size_t val) {
  print << NumberFormat::Hexidecimal << val << "\n";
}

void kernel_debug_delay() {
  for (volatile int i = 0; i < 10000000; i++) {
    asm volatile("nop");
  }
}

}  // extern "C"
