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

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <queue>
#include <set>

#include "elf_file.h"
#include "elf_file_cache.h"
#include "elf_header.h"
#include "file.h"
#include "init_fini_functions.h"
#include "memory.h"
#include "multiboot.h"
#include "perception/auxv.h"
#include "perception/memory.h"
#include "perception/memory_span.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/tracing.h"
#include "process.h"
#include "status.h"
#include "symbol_map.h"
#include "virtual_address_allocator.h"

using ::perception::AllocateMemoryPages;
using ::perception::CreateChildProcess;
using ::perception::DestroyChildProcess;
using ::perception::GetProcessName;
using ::perception::kPageSize;
using ::perception::MemorySpan;
using ::perception::ProcessId;
using ::perception::ReleaseMemoryPages;
using ::perception::StartExecutingChildProcess;

namespace {

// Default name of the window manager application.
constexpr std::string_view kDefaultWindowManager = "Window Manager";

// Name of the terminal application used for running console programs.
constexpr std::string_view kTerminalApplicationName = "Terminal";

// Maximum number of auxiliary vector entries written to the arguments page.
constexpr size_t kMaxAuxvEntries = 8;

// Fixed virtual address where the init and fini arrays are populated.
constexpr size_t kInitFiniAddress = 0x1FF0000;

// Extracts the application name from a program name or path.
std::string_view ExtractApplicationName(std::string_view name_or_path) {
  auto slash_index = name_or_path.find_last_of('/');
  if (slash_index != std::string_view::npos)
    name_or_path = name_or_path.substr(slash_index + 1);
  if (name_or_path.size() > 4 &&
      name_or_path.substr(name_or_path.size() - 4) == ".app") {
    name_or_path = name_or_path.substr(0, name_or_path.size() - 4);
  }
  return name_or_path;
}

// Checks whether the contents of a launcher.json file specify
// `"terminal": true`.
bool DoesLauncherJsonSpecifyTerminal(std::string_view json_content) {
  size_t i = 0;
  while (i < json_content.size()) {
    if (json_content[i] != '"') {
      i++;
      continue;
    }
    i++;
    std::string str_value;
    while (i < json_content.size() && json_content[i] != '"') {
      if (json_content[i] == '\\' && i + 1 < json_content.size())
        i++;
      str_value.push_back(json_content[i]);
      i++;
    }
    if (i < json_content.size()) i++;

    if (str_value != "terminal") continue;

    while (i < json_content.size() &&
           std::isspace(static_cast<unsigned char>(json_content[i]))) {
      i++;
    }
    if (i >= json_content.size() || json_content[i] != ':') continue;
    i++;

    while (i < json_content.size() &&
           std::isspace(static_cast<unsigned char>(json_content[i]))) {
      i++;
    }
    if (json_content.substr(i, 4) == "true") {
      size_t after = i + 4;
      if (after >= json_content.size() ||
          (!std::isalnum(static_cast<unsigned char>(json_content[after])) &&
           json_content[after] != '_')) {
        return true;
      }
    }
  }
  return false;
}

// Returns whether the launch request should be redirected to the Terminal
// application because its launcher.json specifies `"terminal": true` and no
// standard stream pipes are already attached.
bool ShouldLaunchInTerminal(
    const ::perception::LoadApplicationRequest& request) {
  if (IsLoadingMultibootModules()) return false;
  if ((request.stdin_pipe && request.stdin_pipe->GetId() != 0) ||
      (request.stdout_pipe && request.stdout_pipe->GetId() != 0) ||
      (request.stderr_pipe && request.stderr_pipe->GetId() != 0)) {
    return false;
  }

  std::string_view app_name = ExtractApplicationName(request.name);
  if (app_name.empty() || app_name == kTerminalApplicationName ||
      request.name == kTerminalApplicationName) {
    return false;
  }

  std::string launcher_json_path =
      "/Applications/" + std::string(app_name) + "/launcher.json";
  std::ifstream file(launcher_json_path);
  if (!file.is_open() && !request.name.empty() && request.name[0] == '/') {
    std::filesystem::path parent =
        std::filesystem::path(request.name).parent_path();
    if (!parent.empty())
      file.open((parent / "launcher.json").string());
  }
  if (!file.is_open()) return false;

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  return DoesLauncherJsonSpecifyTerminal(content);
}

// Name of the configured window manager application.
std::string g_configured_window_manager{kDefaultWindowManager};

// Mutex protecting g_configured_window_manager.
std::mutex g_window_manager_mutex;

// Reads the configured window manager from the registry.
void UpdateConfiguredWindowManagerFromRegistry() {
  auto status_or_value = ::perception::GetRegistryValue(
      ::perception::RegistryCorpus::APPLICATIONS, "Loader", "windowManager");
  if (status_or_value.Ok() &&
      status_or_value->GetType() ==
          ::perception::serialization::Value::Type::STRING) {
    auto wm = status_or_value->StringValue();
    if (wm.has_value() && !wm->empty()) {
      std::scoped_lock lock(g_window_manager_mutex);
      g_configured_window_manager = std::string(*wm);
      return;
    }
  }
  std::scoped_lock lock(g_window_manager_mutex);
  g_configured_window_manager = std::string(kDefaultWindowManager);
}

// Uncomment to be very verbose with where shared libraries are loaded.
// #define VERBOSE 1

// Loads all of the dependencies for an executable, returning an array
// containing the executable and all depedendecies.
std::optional<std::vector<std::shared_ptr<ElfFile>>>
LoadDependencies(std::shared_ptr<ElfFile> executable_file) {
  PERCEPTION_TRACE_SPAN("LoadDependencies");

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

std::string GetConfiguredWindowManager() {
  std::scoped_lock lock(g_window_manager_mutex);
  return g_configured_window_manager;
}

void InitializeWindowManagerSetting() {
  UpdateConfiguredWindowManagerFromRegistry();
  (void)::perception::RegisterRegistryListener(
      ::perception::RegistryCorpus::APPLICATIONS, "Loader", "windowManager",
      []() { UpdateConfiguredWindowManagerFromRegistry(); });
}

StatusOr<::perception::ProcessId> LoadProgram(
    ::perception::ProcessId creator, std::string_view name,
    const std::vector<std::string>& arguments) {
  ::perception::LoadApplicationRequest request;
  request.name = std::string(name);
  request.arguments = arguments;
  return LoadProgram(request, creator);
}

StatusOr<::perception::ProcessId> LoadProgram(
    const ::perception::LoadApplicationRequest& request,
    ::perception::ProcessId creator) {
  ::perception::LoadApplicationRequest effective_request = request;
  if (ShouldLaunchInTerminal(request)) {
    effective_request = ::perception::LoadApplicationRequest();
    effective_request.name = std::string(kTerminalApplicationName);
    effective_request.arguments.push_back(request.name);
    effective_request.arguments.insert(effective_request.arguments.end(),
                                       request.arguments.begin(),
                                       request.arguments.end());
    effective_request.create_as_child = request.create_as_child;
  }

  std::string_view name = effective_request.name;
  const std::vector<std::string>& arguments = effective_request.arguments;

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
                   name == "AHCI Controller" || name == "Virtio Network" ||
                   GetProcessName(creator) == "Device Manager";
  bool is_window_manager =
      elf_file->File().Name() == GetConfiguredWindowManager();
  bool is_launcher = elf_file->File().Name() == "Launcher";
  bool is_jsshell = elf_file->File().Name() == "jsshell";
  size_t bitfield = 0;
  if (is_driver) bitfield |= ::perception::ProcessBitfield::kIsDriver;
  if (is_window_manager) {
    bitfield |= ::perception::ProcessBitfield::kCanSetFocus;
    bitfield |= ::perception::ProcessBitfield::kCanTerminateProcesses;
  }
  if (is_launcher || is_jsshell)
    bitfield |= ::perception::ProcessBitfield::kCanTerminateProcesses;

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

  // Assign virtual addresses to all dependencies.
  std::vector<size_t> load_addresses_of_elf_files(dependencies.size(), 0);

  // 1. Allocate virtual base addresses for shared libraries
  // (dependencies[1..N]) first
  for (size_t i = 1; i < dependencies.size(); i++) {
    auto library = dependencies[i];
    size_t base_address = library->GetAssignedBaseAddress();
    if (base_address == 0) {
      base_address = VirtualAddressAllocator::Get().AllocateRange(
          library->GetSizeInBytes());
      library->SetAssignedBaseAddress(base_address);
#if VERBOSE
      std::cout << "Library " << library->File().Name() << " assigned to 0x"
                << std::hex << base_address << std::dec << std::endl;
#endif
    }
#if VERBOSE
    std::cout << "Loading " << library->File().Name() << " in process "
              << name << " at 0x" << std::hex << base_address << std::dec
              << std::endl;
#endif
    load_addresses_of_elf_files[i] = base_address;
  }

  // 2. Main executable (dependencies[0]) is loaded at base address 0 (ET_EXEC)
  // or 0x200000 (ET_DYN)
  if (!dependencies.empty()) {
    auto header = dependencies[0]->ElfHeader();
    if (header != nullptr && header->e_type == ET_EXEC) {
      load_addresses_of_elf_files[0] = 0;
    } else {
      load_addresses_of_elf_files[0] = 0x200000;
    }
  }

  // Pass 1: Load in each ELF file into address space and collect exported
  // symbols.
  bool something_went_wrong = false;
  for (size_t i = 0; i < dependencies.size(); i++) {
    auto dependency = dependencies[i];
    size_t base_address = load_addresses_of_elf_files[i];

    auto status_or_next_free_address =
        dependency->LoadIntoAddressSpaceAndReturnNextFreeAddress(
            child_pid, base_address, child_memory_pages, symbols_to_addresses,
            init_fini_functions);

    if (!status_or_next_free_address.Ok()) {
      something_went_wrong = true;
      break;
    }
  }

  if (something_went_wrong) {
    cleanup();
    return Status::INTERNAL_ERROR;
  }

  // Create the init and fini arrays at a fixed address so pre-linked GOT
  // entries in shared libraries resolve consistently across all process
  // instances.
  size_t init_fini_address = kInitFiniAddress;
  size_t next_free_address = init_fini_functions.PopulateInMemory(
      init_fini_address, child_memory_pages, symbols_to_addresses);
  if (next_free_address == 0) {
    cleanup();
    return Status::OUT_OF_MEMORY;
  }

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
            child_pid, child_memory_pages, load_addresses_of_elf_files[i],
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
  size_t auxv_and_ptrs_words = argc + 2 + kMaxAuxvEntries * 2;
  size_t needed_bytes = pointers_offset + 8 * auxv_and_ptrs_words;
  needed_bytes += name.length() + 1;
  for (const auto& arg : arguments)
    needed_bytes += arg.length() + 1;

  size_t needed_pages = (needed_bytes + kPageSize - 1) / kPageSize;

  size_t args_page_address =
      VirtualAddressAllocator::Get().FindFreeRangeWithoutInserting(
          needed_pages * kPageSize);

  char* args_page = (char*)AllocateMemoryPages(needed_pages);
  if (args_page == nullptr) {
    cleanup();
    return Status::OUT_OF_MEMORY;
  }
  for (size_t p = 0; p < needed_pages; p++)
    child_memory_pages[args_page_address + p * kPageSize] =
        args_page + p * kPageSize;
  memset(args_page, 0, needed_pages * kPageSize);

  *(size_t*)args_page = argc;

  size_t strings_offset =
      pointers_offset + 8 * auxv_and_ptrs_words;  // Space for argv, envp, auxv

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
    if (strings_offset + arg.length() + 1 > needed_pages * kPageSize)
      break;
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

  write_aux(3, load_addresses_of_elf_files[0] + phdr_addr);  // AT_PHDR = 3
  write_aux(4, phnum);      // AT_PHNUM = 4
  write_aux(5, phent);      // AT_PHENT = 5
  write_aux(6, kPageSize);  // AT_PAGESZ = 6
  size_t stdin_id = (effective_request.stdin_pipe &&
                     effective_request.stdin_pipe->GetId() != 0)
                        ? effective_request.stdin_pipe->GetId()
                        : 0;
  size_t stdout_id = (effective_request.stdout_pipe &&
                      effective_request.stdout_pipe->GetId() != 0)
                         ? effective_request.stdout_pipe->GetId()
                         : 0;
  size_t stderr_id = (effective_request.stderr_pipe &&
                      effective_request.stderr_pipe->GetId() != 0)
                         ? effective_request.stderr_pipe->GetId()
                         : 0;

  if (stdin_id != 0)
    write_aux(::perception::kAuxvPerceptionStdin, stdin_id);
  if (stdout_id != 0)
    write_aux(::perception::kAuxvPerceptionStdout, stdout_id);
  if (stderr_id != 0)
    write_aux(::perception::kAuxvPerceptionStderr, stderr_id);
  write_aux(0, 0);          // AT_NULL = 0

  size_t args_address = args_page_address;

  // Send the memory pages to the child.
  SendMemoryPagesToChild(child_pid, child_memory_pages);

  // Join standard stream pipes into the child process after all ELF segments,
  // the stack, and the arguments page have been mapped into the child's virtual
  // address space so the kernel's address allocator does not overlap them.
  if (stdin_id != 0)
    effective_request.stdin_pipe->JoinChildProcess(child_pid);
  if (stdout_id != 0 && stdout_id != stdin_id)
    effective_request.stdout_pipe->JoinChildProcess(child_pid);
  if (stderr_id != 0 && stderr_id != stdin_id && stderr_id != stdout_id)
    effective_request.stderr_pipe->JoinChildProcess(child_pid);

  // Remember these dependencies so they stay in memory while the program runs.
  RecordChildPidAndDependencies(child_pid, dependencies,
                                load_addresses_of_elf_files);

  // Creates a thread in the child process. The child process will begin
  // executing and will no longer terminate if the creator terminates.
  StartExecutingChildProcess(
      child_pid, elf_file->EntryAddress(load_addresses_of_elf_files[0]),
      /*params=*/args_address,
      effective_request.create_as_child ? creator : 0);

  for (auto &dependency : dependencies)
    DecrementElfFile(dependency);
  return child_pid;
}
