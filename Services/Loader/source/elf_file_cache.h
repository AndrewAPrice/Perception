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

#include <memory>
#include <string>

#include "elf_file.h"

// Attempts to get an ELF file. Checks if it is already in memory (such as a
// shared library used by a currently running application) first, otherwise
// attempts to load it from. Can return an empty shared_ptr if there is no file
// could be found or loaded. This also automatically increments a reference count
// to the ELF file, so it must be passed back to `DecrementElfFile`.
std::shared_ptr<ElfFile> LoadOrIncrementElfFile(const std::string& name);

// Decrements a reference count to an Elf file, removing it from the cache if it
// reaches 0.
void DecrementElfFile(std::shared_ptr<ElfFile> elf_file);