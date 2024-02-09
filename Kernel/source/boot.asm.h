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

// Pointers for the TSS entry in the GDT. WARNING: These refer to symbols in
// lower memory, so we'll have to add VIRTUAL_MEMORY_OFFSET when deferencing
// them.
extern "C" uint64 TSSEntry;

// boot.asm defines MultibootInfo but that is declared in
// third_party/multiboot2.h.
