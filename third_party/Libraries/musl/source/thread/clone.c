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

#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>

#include "syscall.h"

hidden int __clone(int (*fn)(void*), void* child_stack, int flags, void* arg,
                   ...) {
  va_list ap;
  va_start(ap, arg);
  void* parent_tidptr = va_arg(ap, void*);
  void* tls = va_arg(ap, void*);
  void* child_tidptr = va_arg(ap, void*);
  va_end(ap);

  register size_t syscall_num __asm__("rdi") = 1;
  register size_t r_fn __asm__("rax") = (size_t)fn;
  register size_t r_arg __asm__("rbx") = (size_t)arg;
  register size_t r_stack __asm__("rdx") = (size_t)child_stack;
  register size_t r_tls __asm__("rsi") = (size_t)tls;
  register size_t return_val __asm__("rax");

  __asm__ __volatile__(
      "syscall"
      : "=r"(return_val)
      : "r"(syscall_num), "r"(r_fn), "r"(r_arg), "r"(r_stack), "r"(r_tls)
      : "rcx", "r11", "memory");

  if (return_val == 0) return -EAGAIN;

  if (parent_tidptr) *(int*)parent_tidptr = return_val;

  return return_val;
}
