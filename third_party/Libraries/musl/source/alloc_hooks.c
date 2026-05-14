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

void* malloc(size_t);
void* realloc(void*, size_t);
void* calloc(size_t, size_t);
void free(void*);

__attribute__((visibility("hidden"))) void* __libc_malloc(size_t size) {
  return malloc(size);
}

__attribute__((visibility("hidden"))) void* __libc_realloc(void* ptr,
                                                           size_t size) {
  return realloc(ptr, size);
}

__attribute__((visibility("hidden"))) void* __libc_calloc(size_t nmemb,
                                                          size_t size) {
  return calloc(nmemb, size);
}

__attribute__((visibility("hidden"))) void __libc_free(void* ptr) { free(ptr); }
