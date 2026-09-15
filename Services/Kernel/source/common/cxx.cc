#ifndef TEST
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

// Methods used implicitly by C++.

#include "output/text_terminal.h"

namespace std {

[[noreturn]] void terminate() {
  output::print << "std::terminate() called in kernel.\n";
  asm volatile("cli");
  for (;;) asm volatile("hlt");
}

}  // namespace std

// Called when an unhandled pure virtual function is called.
extern "C" void __cxa_pure_virtual() {
  output::print << "Pure virtual function called in kernel.\n";
  std::terminate();
}

// Registers a destructor to be called on exit.
extern "C" int __cxa_atexit(void (*destructor)(void *), void *arg,
                            void *__dso_handle) {
  // The kernel never exits like a normal program (the computer powers off), so
  // there's nothing to do here.
  return 0;
}

// Begins catching an exception.
extern "C" void* __cxa_begin_catch(void* exceptionObject) {
  output::print << "C++ exception called in the terminal.\n";
  std::terminate();
  return nullptr;
}
#endif // TEST
