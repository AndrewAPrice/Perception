#ifndef TEST
// Copyright 2020 Google LLC
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

#include "output/text_terminal.h"
#include "types.h"

namespace {

// Magic canary value used by stack smashing protection (-fstack-protector).
constexpr uint64 kStackChkGuard = 0x595e9fbd94fda766ULL;

}  // namespace

uint64 __stack_chk_guard = kStackChkGuard;

__attribute__((noreturn)) void __stack_chk_fail(void) {
  asm volatile("cli");
  output::print << "Stack smashing detected.";
  for (;;) asm volatile("hlt");
}

#endif // TEST
