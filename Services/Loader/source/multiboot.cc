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

#include "multiboot.h"

#include <iostream>
#include <map>
#include <string>

#include "elf_file.h"
#include "elf_file_cache.h"
#include "elf_header.h"
#include "loader.h"
#include "perception/multiboot.h"
#include "perception/processes.h"

using ::perception::GetMultibootModule;
using ::perception::GetProcessId;
using ::perception::MultibootModule;

namespace {

// Multiboot modules indexed by name.
std::map<std::string, std::unique_ptr<MultibootModule>>
    multiboot_modules_by_name;

void ParseMultibootModules() {
  while (true) {
    auto multiboot_module = GetMultibootModule();
    if (!multiboot_module) {
      return;
    }

    std::string name = multiboot_module->name;
    multiboot_modules_by_name[name] = std::move(multiboot_module);
  }
}

}  // namespace

void LoadMultibootModules() {
  ParseMultibootModules();

  // Collect a list of program names to load. Do this seperately first then
  // iterate over the names and multiboot_modules_by_name will be mutated as
  // modules are utilized.
  std::vector<std::string> elf_modules;
  for (const auto& name_and_module : multiboot_modules_by_name) {
    const auto& module = name_and_module.second;
    if (IsValidElfFile(module->data))
      elf_modules.push_back(name_and_module.first);
  }

  // Increment a reference count to each of the multiboot ELF files. This is
  // incase one fails to load, the reference to that dependent library isn't
  // automatically released, and all other ELF modules that may depend on it
  // then lose their references.
  std::vector<std::shared_ptr<ElfFile>> multiboot_elf_files;
  std::vector<std::string> programs_to_load;
  for (const std::string& elf_module : elf_modules) {
    auto elf_file = LoadOrIncrementElfFile(elf_module);
    if (elf_file) {
      multiboot_elf_files.push_back(elf_file);
      if (elf_file->IsExecutable()) {
        programs_to_load.push_back(elf_module);
      }
    }
  }

  // Load the multiboot programs.
  auto pid = GetProcessId();
  for (const std::string& program_to_load : programs_to_load)
    (void)LoadElfProgram(pid, program_to_load);

  // Decrease the held references to the multiboot modules.
  std::vector<std::string> mulitboot_files_with_references;
  for (auto& multiboot_elf_file : multiboot_elf_files) {
    DecrementElfFile(multiboot_elf_file);
    if (!multiboot_elf_file->AreThereStillReferences())
      mulitboot_files_with_references.push_back(
          multiboot_elf_file->File().Name());
  }

  if (mulitboot_files_with_references.size() > 0) {
    std::cout
        << "The following multiboot modules aren't referenced by a running "
           "application, so you can remove them from your grub.cfg:"
        << std::endl;
    for (const auto& file : mulitboot_files_with_references)
      std::cout << " * " << file << std::endl;
    std::cout << std::endl;
  }
}

std::unique_ptr<::perception::MultibootModule> GetMultibootModule(
    std::string_view name) {
  auto itr = multiboot_modules_by_name.find(std::string(name));
  if (itr == multiboot_modules_by_name.end()) return {};

  auto multiboot_module = std::move(itr->second);
  multiboot_modules_by_name.erase(itr);
  return multiboot_module;
}
