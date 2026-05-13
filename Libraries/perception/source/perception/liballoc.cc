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

#define USERSPACE_BUILD

#include <stddef.h>

#include "types.h"

#undef KERNEL
#undef PREFIX
#define PREFIX(func) func

#define printf(...)

extern "C" {
void* malloc(size_t);
void* realloc(void*, size_t);
void* calloc(size_t, size_t);
void free(void*);

void* la_malloc(size_t size);
void* la_realloc(void* ptr, size_t size);
void* la_calloc(size_t nmemb, size_t size);
void la_free(void* ptr);

#include "../../../third_party/liballoc.c"

void* la_malloc(size_t size) { return malloc(size); }
void* la_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
void* la_calloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }
void la_free(void* ptr) { free(ptr); }

void* aligned_alloc(size_t alignment, size_t size) { return malloc(size); }
void* memalign(size_t alignment, size_t size) { return malloc(size); }
size_t malloc_usable_size(void* ptr) {
  if (ptr == nullptr) return 0;
  void* orig_ptr = ptr;
  UNALIGN(orig_ptr);
  struct liballoc_minor* min =
      (struct liballoc_minor*)((size_t)orig_ptr -
                               sizeof(struct liballoc_minor));
  if (min->magic == LIBALLOC_MAGIC) {
    return min->size;
  }
  return 0;
}

void* __libc_malloc(size_t size) { return malloc(size); }

void* __libc_realloc(void* ptr, size_t size) { return realloc(ptr, size); }

void* __libc_calloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }

void __libc_free(void* ptr) { free(ptr); }
}

struct InitLiballoc {
  InitLiballoc() { l_pageCount = 256; }
};

static InitLiballoc init_liballoc;
