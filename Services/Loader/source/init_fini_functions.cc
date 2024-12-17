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
#include "init_fini_functions.h"

#include "perception/memory.h"

using ::perception::AllocateMemoryPages;
using ::perception::kPageSize;

void InitFiniFunctions::AddArraySection(
    std::optional<const Elf64_Shdr *> section_header, ArraySection section,
    size_t offset) {
  if (!section_header) return;

  auto array = GetArrayOfArrays(section);
  if (!array) return;

  array->push_back({(*section_header)->sh_addr + offset,
                    (*section_header)->sh_size / sizeof(size_t)});
}

void InitFiniFunctions::AddFunctionSection(
    std::optional<const Elf64_Shdr *> section_header, FunctionSection section,
    size_t offset) {
  if (!section_header) return;

  auto array = GetArrayOfFunctions(section);
  if (!array) return;

  array->push_back((*section_header)->sh_addr + offset);
}

void InitFiniFunctions::PopulateInMemory(
    size_t start_address, std::map<size_t, void *> &child_memory_pages,
    std::map<std::string, size_t> &symbols_to_addresses) {
  // Create the first page.
  size_t write_address = start_address;
  size_t write_page_index = 0xFFFFFFFFFFFFFFFF;
  char *write_page = nullptr;

  // Switches to a page.
  auto switch_to_page = [&](size_t address, size_t &page_index,
                            char *&page_ptr) {
    size_t new_page_index = address / kPageSize;
    if (page_index != new_page_index) {
      page_index = new_page_index;

      size_t page_start_addr = page_index * kPageSize;
      auto itr = child_memory_pages.find(page_start_addr);
      if (itr == child_memory_pages.end()) {
        page_ptr = (char *)AllocateMemoryPages(1);
        child_memory_pages[page_start_addr] = page_ptr;
      } else {
        page_ptr = (char *)itr->second;
      }
    }
  };

  // Writes a value and increments to write address.
  auto write_value = [&](size_t value) {
    switch_to_page(write_address, write_page_index, write_page);

    size_t index_in_page = write_address % kPageSize;

    *(size_t *)&write_page[index_in_page] = value;

    write_address += sizeof(size_t);
  };

  // Writes an array of array of functions.
  auto write_array_of_arrays =
      [&](const std::vector<std::pair<size_t, size_t>> &arrays) {
        write_value(arrays.size());
        for (const auto [address, length] : arrays) {
          write_value(address);
          write_value(length);
        }
      };

  // Writes an array of functions.
  auto write_appended_functions = [&](const std::vector<size_t> &functions) {
    write_value(functions.size());
    for (auto function_address : functions) write_value(function_address);
  };

  symbols_to_addresses["__preinit_array_of_arrays"] = write_address;
  write_array_of_arrays(preinit_arrays_);

  symbols_to_addresses["__init_array_of_arrays"] = write_address;
  write_array_of_arrays(init_arrays_);

  symbols_to_addresses["__fini_array_of_arrays"] = write_address;
  write_array_of_arrays(fini_arrays_);

  symbols_to_addresses["__init_functions"] = write_address;
  write_appended_functions(init_functions_);

  symbols_to_addresses["__fini_functions"] = write_address;
  write_appended_functions(fini_functions_);
}

std::vector<std::pair<size_t, size_t>> *InitFiniFunctions::GetArrayOfArrays(
    ArraySection section) {
  switch (section) {
    case PreInitArray:
      return &preinit_arrays_;
    case InitArray:
      return &init_arrays_;
    case FiniArray:
      return &fini_arrays_;
    default:
      return nullptr;
  }
}

std::vector<size_t> *InitFiniFunctions::GetArrayOfFunctions(
    FunctionSection section) {
  switch (section) {
    case Init:
      return &init_functions_;
    case Fini:
      return &fini_functions_;
    default:
      return nullptr;
  }
}
