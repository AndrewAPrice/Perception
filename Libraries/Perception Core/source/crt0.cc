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

#include "types.h"

// Some compilers mangle `main` (such as clang), others don't (such as GCC). To
// be consistent, expose a weak `_main` to crt0.asm to calls the mangled main.
int main(int argc, char *argv[]);

extern "C" {

extern void _init();

// Exported symbols.
extern __attribute__((__weak__)) size_t __init_array_start, __init_array_end,
    __fini_array_start, __fini_array_end, __fini_array_start, __fini_array_end,
    __preinit_array_of_arrays, __init_array_of_arrays, __fini_array_of_arrays,
    __init_functions, __fini_functions;
}

namespace {

// Calls an array of functions.
void CallArrayOfArrayOfFunctions(size_t *address) {
  size_t init_arrays_count = *(address++);

  for (; init_arrays_count > 0; init_arrays_count--) {
    size_t array_addr = *(address++);
    size_t array_length = *(address++);

    for (; array_length > 0; array_length--, array_addr += 8)
      (*(void (**)(void))array_addr)();
  }
}

// Jumps to a function, rather than calls it.
void JumpToFunction(size_t address) {
  __asm__ volatile("jmp *%0" : : "r"(address) :);
}

// Calls an array of functions.
void CallArrayOfFunctions(size_t *address) {
  size_t init_arrays_count = *(address++);

  for (; init_arrays_count > 0; init_arrays_count--) {
    size_t function_address = *(address++);
    JumpToFunction(function_address);
  }
}

void CallArrayOfFunctions(size_t *first_address_ptr, size_t *last_address_ptr) {
  for (size_t address_ptr = (size_t)first_address_ptr;
       address_ptr < (size_t)last_address_ptr; address_ptr += sizeof(size_t))
    (*(void (**)(void))address_ptr)();
}

// Returns whether is is a statically linked application.
bool IsStaticallyLinked() {
  // The Perception loader populates __init_array_of_arrays.
  return (size_t *)&__init_array_of_arrays == 0;
}

}  // namespace

extern "C" {

// Calls global initializers.
__attribute__((visibility("default"))) void __libc_start_init() {
  _init();

  if (IsStaticallyLinked()) {
    CallArrayOfFunctions(&__init_array_start, &__init_array_end);
  } else {
    CallArrayOfArrayOfFunctions(&__preinit_array_of_arrays);
    CallArrayOfFunctions(&__init_functions);
    CallArrayOfArrayOfFunctions(&__init_array_of_arrays);
  }
}

// Calls global finalizers.
__attribute__((visibility("default"))) void __libc_exit_fini() {
  if (IsStaticallyLinked()) {
    CallArrayOfFunctions(&__fini_array_start, &__fini_array_end);
  } else {
    CallArrayOfFunctions(&__fini_functions);
    CallArrayOfArrayOfFunctions(&__fini_array_of_arrays);
  }
}

// Calls main.
void _main(int argc, char *argv[]) __attribute__((weak)) { main(argc, argv); }

}