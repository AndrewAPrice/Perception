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

#pragma once

#include "types.h"

namespace perception {

void EnableProfiling() {
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 55;
  __asm__ __volatile__("syscall\n" ::"r"(syscall) : "rcx", "r11");
#endif
}

void DisableAndOutputProfiling() {
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 56;
  __asm__ __volatile__("syscall\n" ::"r"(syscall) : "rcx", "r11");
#endif
}

}
