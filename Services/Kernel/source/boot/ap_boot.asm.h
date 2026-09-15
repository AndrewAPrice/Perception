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

extern "C" {

// Entry point and boundary symbols for the real-mode AP trampoline.
extern uint8 ap_trampoline_start[];
extern uint8 ap_trampoline_end[];

// Secondary core C++ entry point invoked from ap_boot.asm.
void ApMain(size_t core_id);

}  // extern "C"
