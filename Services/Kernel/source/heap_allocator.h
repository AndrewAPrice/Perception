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

#include "types.h"

#if defined(TEST)
#include <stdlib.h>
#else

extern "C" {
void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t nmemb, size_t size);
}

// To support placement new.
inline void *operator new(unsigned long, void *p) throw() { return p; }
inline void *operator new[](unsigned long, void *p) throw() { return p; }
inline void operator delete(void *, void *) throw() {};
inline void operator delete[](void *, void *) throw() {};

inline bool AreInterruptsEnabled() {
  size_t rflags;
  asm volatile("pushfq\n\tpop %0" : "=r"(rflags));
  return (rflags & (1 << 9)) != 0;
}

#define BEGIN_NO_INTERRUPTS() \
  bool interrupts_were_enabled = AreInterruptsEnabled(); \
  asm volatile("cli" ::: "memory");

#define END_NO_INTERRUPTS() \
  if (interrupts_were_enabled) { \
    asm volatile("sti" ::: "memory"); \
  }

#endif

