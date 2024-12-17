// Copyright 2021 Google LLC
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

#include "loader.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <queue>
#include <set>

#include "elf_file.h"
#include "elf_file_cache.h"
#include "elf_header.h"
#include "file.h"
#include "memory.h"
#include "multiboot.h"
#include "perception/memory.h"
#include "perception/memory_span.h"
#include "perception/multiboot.h"
#include "perception/processes.h"
#include "status.h"

using ::perception::AllocateMemoryPages;
using ::perception::CreateChildProcess;
using ::perception::DestroyChildProcess;
using ::perception::GetProcessName;
using ::perception::kPageSize;
using ::perception::MemorySpan;
using ::perception::ProcessId;
using ::perception::ReleaseMemoryPages;
using ::perception::StartExecutingChildProcess;
using ::perception::Status;

namespace {

std::optional<std::vector<std::shared_ptr<ElfFile>>> LoadDependencies(
    std::shared_ptr<ElfFile> executable_file) {
  std::set<std::string> loaded_dependencies;
  std::queue<std::string> dependencies_to_load;

  executable_file->ForEachDependentLibrary([&](std::string_view library_sv) {
    //     std::cout << "Dependent library: " << library_sv << std::endl;
    std::string library_name = std::string(library_sv);
    if (loaded_dependencies.contains(library_name)) return;
    //   std::cout << "queuing: " << library_sv << std::endl;

    loaded_dependencies.insert(library_name);
    dependencies_to_load.push(library_name);
  });

  std::vector<std::shared_ptr<ElfFile>> loaded_elf_files;

  while (!dependencies_to_load.empty()) {
    std::string name = dependencies_to_load.front();
    auto elf_library = LoadOrIncrementElfFile(name);
    dependencies_to_load.pop();

    if (!elf_library) {
      std::cout << "Cannot load dependency of "
                << executable_file->File().Name() << ": " << name;

      // Unload all loaded files.
      for (auto& loaded_file : loaded_elf_files) DecrementElfFile(loaded_file);
      return std::nullopt;
    }

    loaded_elf_files.push_back(elf_library);

    elf_library->ForEachDependentLibrary([&](std::string_view library_sv) {
      std::string library_name = std::string(library_sv);
      if (loaded_dependencies.contains(library_name)) return;

      loaded_dependencies.insert(library_name);
      dependencies_to_load.push(library_name);
    });
  }

  return loaded_elf_files;
}

void CreateInitAndFiniArrays(
    const std::vector<std::pair<size_t, size_t>>& preinit_arrays,
    const std::vector<std::pair<size_t, size_t>> init_functions,
    const std::vector<std::pair<size_t, size_t>>& init_arrays,
    const std::vector<std::pair<size_t, size_t>>& fini_functions,
    const std::vector<std::pair<size_t, size_t>>& fini_arrays,
    size_t start_address, std::map<size_t, void*>& child_memory_pages,
    std::map<std::string, size_t>& symbols_to_addresses) {
  // Create the first page.
  size_t write_address = start_address;
  size_t write_page_index = 0xFFFFFFFFFFFFFFFF,
         read_page_index = 0xFFFFFFFFFFFFFFFF;
  char *write_page = nullptr, *read_page = nullptr;

  int last_size = sizeof(size_t);

  auto switch_to_page = [&](size_t address, size_t& page_index, char*& page_ptr,
                            bool allocate_if_missing) {
    size_t new_page_index = address / kPageSize;
    if (page_index != new_page_index) {
      page_index = new_page_index;

      size_t page_start_addr = page_index * kPageSize;
      auto itr = child_memory_pages.find(page_start_addr);
      if (itr == child_memory_pages.end()) {
        if (allocate_if_missing) {
          page_ptr = (char*)AllocateMemoryPages(1);
          child_memory_pages[page_start_addr] = page_ptr;
        } else {
          page_ptr = nullptr;
        }
      } else {
        page_ptr = (char*)itr->second;
      }
    }
  };

  // Function to write a value.
  auto write_value = [&](size_t value, int size) {
    if (size > last_size) {
      // Can't grow size, otherwise it's no longer page aligned.
      return;
    } else {
      last_size = size;
    }
    switch_to_page(write_address, write_page_index, write_page,
                   /*allocate_if_missing=*/true);

    size_t index_in_page = write_address % kPageSize;

    if (size == 8) {
      *(size_t*)&write_page[index_in_page] = value;
    } else if (size == 1) {
      std::cout << " 0x" << std::hex << (size_t)value << ' ';
      *(unsigned char*)&write_page[index_in_page] = (unsigned char)value;
    } else {
      // Uknown size.
    }

    write_address += size;
  };

  auto read_byte = [&](size_t address) -> unsigned char {
    switch_to_page(address, read_page_index, read_page,
                   /*allocate_if_missing=*/false);
    if (read_page == nullptr) return 0;
    size_t index_in_page = address % kPageSize;
    return *(unsigned char*)&read_page[index_in_page];
  };

  // Function to write an array.
  auto write_array_of_arrays =
      [&](const std::vector<std::pair<size_t, size_t>>& arrays) {
        write_value(arrays.size(), sizeof(size_t));
        for (const auto& pair : arrays) {
          write_value(pair.first, sizeof(size_t));
          write_value(pair.second, sizeof(size_t));
        }
      };

  auto write_appended_functions =
      [&](const std::vector<std::pair<size_t, size_t>>& functions) {
        write_value(functions.size(), sizeof(size_t));
        for (auto function : functions)
          write_value(function.first, sizeof(size_t));
        /**
        std::cout << "Writing function at " << std::hex << write_address
                  << std::endl;
        for (auto function : functions) {
          // std::cout << "function length at " << std::hex << write_address <<
          // std::endl;
          for (auto [address, length] = function; length > 0;
               length--, address++) {
            std::cout << std::hex << " " << address << ":";
            write_value((size_t)read_byte(address), 1);
          }
        }

        // Write a near return.
        write_value(0xC3, 1);
        */
      };

  symbols_to_addresses["__preinit_array_of_arrays"] = write_address;
  write_array_of_arrays(preinit_arrays);

  symbols_to_addresses["__init_array_of_arrays"] = write_address;
  write_array_of_arrays(init_arrays);

  symbols_to_addresses["__fini_array_of_arrays"] = write_address;
  write_array_of_arrays(fini_arrays);

  symbols_to_addresses["__init_functions"] = write_address;
  write_appended_functions(init_functions);

  symbols_to_addresses["__fini_functions"] = write_address;
  write_appended_functions(fini_functions);
}

}  // namespace

StatusOr<::perception::ProcessId> LoadElfProgram(
    ::perception::ProcessId creator, std::string_view name) {
  auto elf_file = LoadOrIncrementElfFile(std::string(name));
  if (!elf_file) {
    std::cout << "Cannot find ELF file for " << name << std::endl;
    return Status::FILE_NOT_FOUND;
  }

  std::function<void()> cleanup = [&]() { DecrementElfFile(elf_file); };

  if (!elf_file->IsExecutable()) {
    std::cout << "Not an executable file: " << elf_file->File().Path()
              << std::endl;
    cleanup();
    return Status::FILE_NOT_FOUND;
  }

  auto opt_dependencies = LoadDependencies(elf_file);
  if (!opt_dependencies) {
    std::cout << "Cannot load dependencies of executable file: "
              << elf_file->File().Path() << std::endl;
    cleanup();
    return Status::FILE_NOT_FOUND;
  }

  auto dependencies = std::move(*opt_dependencies);
  cleanup = [&]() {
    DecrementElfFile(elf_file);
    for (auto& dependency : dependencies) DecrementElfFile(dependency);
  };

  // Detecting if something is a driver if the device manager launches it
  // is a temporary solution.
  bool is_driver = GetProcessName(creator) == "Device Manager";
  size_t bitfield = is_driver ? (1 << 0) : 0;

  // Create the child process.
  std::cout << "Loading " << (is_driver ? "driver " : "application ")
            << elf_file->File().Name() << "..." << std::endl;

  ProcessId child_pid;
  if (!CreateChildProcess(elf_file->File().Name(), bitfield, child_pid)) {
    std::cout << "Cannot spawn new process to load: "
              << elf_file->File().Path();
    cleanup();
    return Status::INTERNAL_ERROR;
  }

  std::map<size_t, void*> child_memory_pages;
  std::map<std::string, size_t> symbols_to_addresses;

  // From this point on, child_memory_pages must be cleaned up before returning
  // if the child process isn't succesfully spawned.
  cleanup = [&]() {
    for (const auto address_and_page : child_memory_pages)
      ReleaseMemoryPages(address_and_page.second, 1);

    DestroyChildProcess(child_pid);
    DecrementElfFile(elf_file);
    for (auto& dependency : dependencies) DecrementElfFile(dependency);
  };

  std::vector<std::pair<size_t, size_t>> preinit_arrays, init_arrays,
      fini_arrays;
  std::vector<std::pair<size_t, size_t>> init_functions, fini_functions;

  // Load in the main executable.
  auto status_or_next_free_address =
      elf_file->LoadIntoAddressSpaceAndReturnNextFreeAddress(
          child_pid, /*offset=*/0, child_memory_pages, symbols_to_addresses,
          preinit_arrays, init_functions, init_arrays, fini_functions,
          fini_arrays);
  if (!status_or_next_free_address.Ok()) {
    cleanup();
    return Status::INTERNAL_ERROR;
  }

  // Load in the shared libraries.
  size_t next_free_address = *status_or_next_free_address;
  bool something_went_wrong = false;
  std::vector<size_t> load_addresses_of_libraries;
  load_addresses_of_libraries.reserve(dependencies.size());
  for (auto dependency : dependencies) {
    load_addresses_of_libraries.push_back(next_free_address);
    status_or_next_free_address =
        dependency->LoadIntoAddressSpaceAndReturnNextFreeAddress(
            child_pid, next_free_address, child_memory_pages,
            symbols_to_addresses, preinit_arrays, init_functions, init_arrays,
            fini_functions, fini_arrays);

    if (!status_or_next_free_address.Ok()) {
      something_went_wrong = true;
      break;
    }
    next_free_address = *status_or_next_free_address;
  }

  if (something_went_wrong) {
    cleanup();
    return Status::INTERNAL_ERROR;
  }

  // Create the init and fini arrays.
  CreateInitAndFiniArrays(preinit_arrays, init_functions, init_arrays,
                          fini_functions, fini_arrays, next_free_address,
                          child_memory_pages, symbols_to_addresses);

  // Fix up the main executable.
  if (elf_file->FixUpRelocations(child_memory_pages, /*offset=*/0,
                                 symbols_to_addresses,
                                 /*module_id=*/0) != Status::OK) {
    cleanup();
    return Status::INTERNAL_ERROR;
  }

  // Fix up the shared libraries.
  for (int i = 0; i < dependencies.size(); i++) {
    if (dependencies[i]->FixUpRelocations(
            child_memory_pages, load_addresses_of_libraries[i],
            symbols_to_addresses, /*module_id=*/i + 1) != Status::OK) {
      something_went_wrong = true;
      break;
    }
  }

  if (something_went_wrong) {
    cleanup();
    return Status::INTERNAL_ERROR;
  }

  // Send the memory pages to the child.
  SendMemoryPagesToChild(child_pid, child_memory_pages);

  // TODO: Increment all dependencies, and listen for when the process exits to
  // decrement them.

  // Creates a thread in the a child process. The child process will begin
  // executing and will no longer terminate if the creator terminates.
  StartExecutingChildProcess(child_pid, elf_file->EntryAddress(/*offset=*/0),
                             /*params=*/0);

  DecrementElfFile(elf_file);
  for (auto& dependency : dependencies) DecrementElfFile(dependency);
  return Status::OK;
}
