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

#include <iostream>
#include <optional>
#include <queue>
#include <set>

#include "elf_file.h"
#include "elf_file_cache.h"
#include "elf_header.h"
#include "file.h"
#include "init_fini_functions.h"
#include "memory.h"
#include "perception/memory.h"
#include "perception/memory_span.h"
#include "perception/processes.h"
#include "process.h"
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

// Uncomment to be very verbose with where shared libraries are loaded.
// #define VERBOSE 1

// Loads all of the dependencies for an executable, returning an array
// containing the executable and all depedendecies.
std::optional<std::vector<std::shared_ptr<ElfFile>>>
LoadDependencies(std::shared_ptr<ElfFile> executable_file) {
  std::set<std::string> loaded_dependencies;
  std::queue<std::string> dependencies_to_load;

  executable_file->ForEachDependentLibrary([&](std::string_view library_sv) {
    //     std::cout << "Dependent library: " << library_sv << std::endl;
    std::string library_name = std::string(library_sv);
    if (loaded_dependencies.contains(library_name))
      return;
    //   std::cout << "queuing: " << library_sv << std::endl;

    loaded_dependencies.insert(library_name);
    dependencies_to_load.push(library_name);
  });

  std::vector<std::shared_ptr<ElfFile>> loaded_elf_files;
  loaded_elf_files.push_back(executable_file);

  while (!dependencies_to_load.empty()) {
    std::string name = dependencies_to_load.front();
    auto elf_library = LoadOrIncrementElfFile(name);
    dependencies_to_load.pop();

    if (!elf_library) {
      std::cout << "Cannot load dependency of "
                << executable_file->File().Name() << ": " << name;

      // Unload all loaded files.
      for (auto &loaded_file : loaded_elf_files)
        DecrementElfFile(loaded_file);
      return std::nullopt;
    }

    loaded_elf_files.push_back(elf_library);

    elf_library->ForEachDependentLibrary([&](std::string_view library_sv) {
      std::string library_name = std::string(library_sv);
      if (loaded_dependencies.contains(library_name))
        return;

      loaded_dependencies.insert(library_name);
      dependencies_to_load.push(library_name);
    });
  }

  return loaded_elf_files;
}

} // namespace

StatusOr<::perception::ProcessId> LoadProgram(
    ::perception::ProcessId creator, std::string_view name,
    const std::vector<std::string>& arguments) {
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
    // elf_file is cleaned up in LoadDependencies.
    return Status::FILE_NOT_FOUND;
  }

  auto dependencies = std::move(*opt_dependencies);
  cleanup = [&]() {
    for (auto &dependency : dependencies)
      DecrementElfFile(dependency);
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

  std::map<size_t, void *> child_memory_pages;
  std::map<std::string_view, size_t> symbols_to_addresses;

  // From this point on, child_memory_pages must be cleaned up before returning
  // if the child process isn't succesfully spawned.
  cleanup = [&]() {
    for (const auto address_and_page : child_memory_pages)
      ReleaseMemoryPages(address_and_page.second, 1);

    DestroyChildProcess(child_pid);
    for (auto &dependency : dependencies)
      DecrementElfFile(dependency);
  };

  InitFiniFunctions init_fini_functions;

  // Load in each ELF file.
  size_t next_free_address = 0;
  bool something_went_wrong = false;
  std::vector<size_t> load_addresses_of_elf_files;
  load_addresses_of_elf_files.reserve(dependencies.size());
  for (auto dependency : dependencies) {
    load_addresses_of_elf_files.push_back(next_free_address);
#if VERBOSE
    std::cout << "Loading " << dependency->File().Name() << " in process "
              << name << " at " << std::hex << next_free_address << std::dec
              << std::endl;
#endif
    auto status_or_next_free_address =
        dependency->LoadIntoAddressSpaceAndReturnNextFreeAddress(
            child_pid, next_free_address, child_memory_pages,
            symbols_to_addresses, init_fini_functions);

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
  next_free_address = init_fini_functions.PopulateInMemory(
      next_free_address, child_memory_pages, symbols_to_addresses);

  // Fix up the ELF files.
  for (int i = 0; i < dependencies.size(); i++) {
    if (dependencies[i]->FixUpRelocations(
            child_memory_pages, load_addresses_of_elf_files[i],
            symbols_to_addresses, /*module_id=*/i + 1) != Status::OK) {
      something_went_wrong = true;
      break;
    }
  }

  if (something_went_wrong) {
    cleanup();
    return Status::INTERNAL_ERROR;
  }

  size_t args_address = 0;
  if (!arguments.empty()) {
    size_t args_page_address =
        (next_free_address + kPageSize - 1) & ~(kPageSize - 1);
    next_free_address = args_page_address + kPageSize;

    char* args_page = (char*)AllocateMemoryPages(1);
    child_memory_pages[args_page_address] = args_page;
    memset(args_page, 0, kPageSize);

    size_t argc = arguments.size() + 1;
    *(size_t*)args_page = argc;

    size_t pointers_offset = 8;
    size_t strings_offset = pointers_offset + 8 * (argc + 4);

    // Write argv[0] pointing to the program name
    size_t child_string_address = args_page_address + strings_offset;
    *(size_t*)(args_page + pointers_offset) = child_string_address;
    if (strings_offset + name.length() + 1 <= kPageSize) {
      memcpy(args_page + strings_offset, name.data(), name.length());
      args_page[strings_offset + name.length()] = '\0';
      strings_offset += name.length() + 1;
    }

    // Write argv[1...] pointing to the arguments
    for (size_t i = 0; i < arguments.size(); ++i) {
      child_string_address = args_page_address + strings_offset;
      *(size_t*)(args_page + pointers_offset + (i + 1) * 8) =
          child_string_address;

      std::string_view arg = arguments[i];
      if (strings_offset + arg.length() + 1 > kPageSize) {
        break;
      }
      memcpy(args_page + strings_offset, arg.data(), arg.length());
      args_page[strings_offset + arg.length()] = '\0';
      strings_offset += arg.length() + 1;
    }
    args_address = args_page_address;
  }

  // Send the memory pages to the child.
  SendMemoryPagesToChild(child_pid, child_memory_pages);

  // Remember these dependencies so they stay in memory while the program runs.
  RecordChildPidAndDependencies(child_pid, dependencies);

  // Creates a thread in the a child process. The child process will begin
  // executing and will no longer terminate if the creator terminates.
  StartExecutingChildProcess(child_pid, elf_file->EntryAddress(/*offset=*/0),
                             /*params=*/args_address);

  for (auto &dependency : dependencies)
    DecrementElfFile(dependency);
  return Status::OK;
}
