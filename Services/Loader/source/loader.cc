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
#include "symbol_map.h"

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
    std::string library_name = std::string(library_sv);
    if (loaded_dependencies.contains(library_name)) return;

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
                << executable_file->File().Name() << ": " << name << std::endl;

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
  bool is_driver = name == "Device Manager" || name == "IDE Controller" ||
                   name == "Virtio Network" ||
                   GetProcessName(creator) == "Device Manager";
  size_t bitfield = is_driver ? (1 << 0) : 0;

  // Create the child process.
  std::cout << "Loading " << (is_driver ? "driver " : "application ")
            << elf_file->File().Name() << "..." << std::endl;

  ProcessId child_pid;
  if (!CreateChildProcess(elf_file->File().Name(), bitfield, child_pid)) {
    std::cout << "Cannot spawn new process to load: " << elf_file->File().Path()
              << std::endl;
    cleanup();
    return Status::INTERNAL_ERROR;
  }

  std::map<size_t, void *> child_memory_pages;
  // Force rebuild comment
  SymbolMap symbols_to_addresses;

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
  size_t next_free_address = 0x200000;
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

  // Calculate correct TLS module IDs (1-indexed for modules that have TLS).
  std::vector<size_t> tls_module_ids(dependencies.size(), 0);
  size_t next_tls_module_id = 1;
  for (int i = 0; i < dependencies.size(); i++) {
    bool has_tls = false;
    for (const auto& segment_header :
         dependencies[i]->ProgramSegmentHeaders()) {
      if (segment_header.p_type == PT_TLS) {
        has_tls = true;
        break;
      }
    }
    if (has_tls) tls_module_ids[i] = next_tls_module_id++;
  }

  // Calculate static TLS offsets of each module in the child process's TLS
  // block.
  std::map<size_t, size_t> load_address_to_tls_offset;
  size_t current_tls_offset = 0;
  for (int i = 0; i < dependencies.size(); i++) {
    const Elf64_Phdr* tls_phdr = nullptr;
    for (const auto& phdr : dependencies[i]->ProgramSegmentHeaders()) {
      if (phdr.p_type == PT_TLS) {
        tls_phdr = &phdr;
        break;
      }
    }
    if (tls_phdr != nullptr) {
      size_t align = tls_phdr->p_align;
      if (align < 8) align = 8;
      size_t size = tls_phdr->p_memsz;
      current_tls_offset =
          (current_tls_offset + size + align - 1) & ~(align - 1);
      load_address_to_tls_offset[load_addresses_of_elf_files[i]] =
          current_tls_offset;
    }
  }

  // Fix up the ELF files.
  for (int i = 0; i < dependencies.size(); i++) {
    if (dependencies[i]->FixUpRelocations(
            child_memory_pages, load_addresses_of_elf_files[i],
            symbols_to_addresses, tls_module_ids[i],
            load_address_to_tls_offset) != Status::OK) {
      something_went_wrong = true;
      break;
    }
  }

  if (something_went_wrong) {
    cleanup();
    return Status::INTERNAL_ERROR;
  }

  size_t argc = arguments.size() + 1;
  size_t pointers_offset = 8;
  size_t needed_bytes = pointers_offset + 8 * (argc + 12);
  needed_bytes += name.length() + 1;
  for (const auto& arg : arguments) {
    needed_bytes += arg.length() + 1;
  }

  size_t needed_pages = (needed_bytes + kPageSize - 1) / kPageSize;

  size_t args_page_address =
      (next_free_address + kPageSize - 1) & ~(kPageSize - 1);
  next_free_address = args_page_address + needed_pages * kPageSize;

  char* args_page = (char*)AllocateMemoryPages(needed_pages);
  for (size_t p = 0; p < needed_pages; p++) {
    child_memory_pages[args_page_address + p * kPageSize] = args_page + p * kPageSize;
  }
  memset(args_page, 0, needed_pages * kPageSize);

  *(size_t*)args_page = argc;

  size_t strings_offset =
      pointers_offset + 8 * (argc + 12);  // Space for argv, envp, auxv

  // Write argv[0] pointing to the program name
  size_t child_string_address = args_page_address + strings_offset;
  *(size_t*)(args_page + pointers_offset) = child_string_address;
  if (strings_offset + name.length() + 1 <= needed_pages * kPageSize) {
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
    if (strings_offset + arg.length() + 1 > needed_pages * kPageSize) {
      break;
    }
    memcpy(args_page + strings_offset, arg.data(), arg.length());
    args_page[strings_offset + arg.length()] = '\0';
    strings_offset += arg.length() + 1;
  }

  // Write auxiliary vector entries
  size_t write_auxv_offset = pointers_offset + 8 * (argc + 2);
  auto write_aux = [&](size_t type, size_t value) {
    *(size_t*)(args_page + write_auxv_offset) = type;
    *(size_t*)(args_page + write_auxv_offset + 8) = value;
    write_auxv_offset += 16;
  };

  // Find the phdr details for the executable
  size_t phdr_addr = 0;
  size_t phnum = elf_file->ElfHeader()->e_phnum;
  size_t phent = elf_file->ElfHeader()->e_phentsize;

  for (const auto& phdr : elf_file->ProgramSegmentHeaders()) {
    if (phdr.p_type == PT_PHDR) {
      phdr_addr = phdr.p_vaddr;
      break;
    }
  }
  if (phdr_addr == 0) {
    for (const auto& phdr : elf_file->ProgramSegmentHeaders()) {
      if (phdr.p_type == PT_LOAD && phdr.p_offset == 0) {
        phdr_addr = phdr.p_vaddr + elf_file->ElfHeader()->e_phoff;
        break;
      }
    }
  }

  write_aux(3, phdr_addr);  // AT_PHDR = 3
  write_aux(4, phnum);      // AT_PHNUM = 4
  write_aux(5, phent);      // AT_PHENT = 5
  write_aux(6, kPageSize);  // AT_PAGESZ = 6
  write_aux(0, 0);          // AT_NULL = 0

  size_t args_address = args_page_address;

  // Send the memory pages to the child.
  SendMemoryPagesToChild(child_pid, child_memory_pages);

  // Remember these dependencies so they stay in memory while the program runs.
  RecordChildPidAndDependencies(child_pid, dependencies);

  // Creates a thread in the a child process. The child process will begin
  // executing and will no longer terminate if the creator terminates.
  StartExecutingChildProcess(
      child_pid, elf_file->EntryAddress(load_addresses_of_elf_files[0]),
      /*params=*/args_address);

  for (auto &dependency : dependencies)
    DecrementElfFile(dependency);
  return child_pid;
}
