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

#include <map>
#include <optional>
#include <vector>

#include "elf.h"
#include "types.h"

// Represents a set of init and fini ELF functions.
class InitFiniFunctions {
 public:
  // The array sections.
  enum ArraySection { PreInitArray, InitArray, FiniArray };

  // The function section.
  enum FunctionSection { Init, Fini };

  // Maybe adds an array section that has been loaded at the given `offset`.
  void AddArraySection(std::optional<const Elf64_Shdr*> section_header,
                       ArraySection section, size_t offset);

  // Maybe adds a function section that has been loaded at the given `offset`.
  void AddFunctionSection(std::optional<const Elf64_Shdr*> section_header,
                          FunctionSection section, size_t offset);

  // Populates a table of arrays and functions in a child process's memory at
  // `start_address`. Allocates the memory in `child_memory_pages`. Populates
  // the symbols to these tables in `symbols_to_addresses`.
  void PopulateInMemory(size_t start_address,
                        std::map<size_t, void*>& child_memory_pages,
                        std::map<std::string, size_t>& symbols_to_addresses);

 private:
  // Returns a pointer to an array of arrays for a given section. Returns
  // nullptr for an invalid section.
  std::vector<std::pair<size_t, size_t>>* GetArrayOfArrays(
      ArraySection section);

  // Returns a pointer to an array functions for a given section. Returns
  // nullptr for an invalid section.
  std::vector<size_t>* GetArrayOfFunctions(FunctionSection section);

  // Array of array of functions (address of array and number of elements in the
  // array).
  std::vector<std::pair<size_t, size_t>> preinit_arrays_, init_arrays_,
      fini_arrays_;

  // Array of functions.
  std::vector<size_t> init_functions_, fini_functions_;
};
