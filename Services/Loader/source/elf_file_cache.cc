
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

#include "elf_file_cache.h"

#include <iostream>
#include <map>
#include <string>

namespace {

// Loaded ELF files by name.
std::multimap<std::string, std::shared_ptr<ElfFile>> elf_files_by_name;

// Loaded ELF files by path.
std::multimap<std::string, std::shared_ptr<ElfFile>> elf_files_by_path;

// Returns an ELF file if it is cached. First checks by name, then by path.
// Returns an empty shared_ptr if no ELF file can be found.
std::shared_ptr<ElfFile> GetCachedElfFile(const std::string& name) {
  auto itr_by_name = elf_files_by_name.find(name);
  if (itr_by_name != elf_files_by_name.end()) return itr_by_name->second;

  auto itr_by_path = elf_files_by_path.find(name);
  if (itr_by_path != elf_files_by_path.end()) return itr_by_path->second;

  return {};
}

// Loads an ELF file (by name or path), then caches and returns it. Returns an
// empty shared_ptr if the ELF file cannot be loaded.
std::shared_ptr<ElfFile> LoadAndCacheElfFile(const std::string& name) {
  auto file = LoadFile(name);
  if (!file) {
    std::cout << "Cannot load file: " << name << std::endl;
    return {};
  }
  auto elf_file = std::make_shared<ElfFile>(std::move(file));
  if (!elf_file->IsValid()) {
    std::cout << "Elf file is not vaild: " << elf_file->File().Path()
              << std::endl;
    return {};
  }

  elf_files_by_name.insert({elf_file->File().Name(), elf_file});
  elf_files_by_path.insert({elf_file->File().Path(), elf_file});
  return elf_file;
}

}  // namespace

std::shared_ptr<ElfFile> LoadOrIncrementElfFile(const std::string& name) {
  // Look for a cached ELF file.
  auto elf_file = GetCachedElfFile(name);
  // Load an ELF file if there was no cached file.
  if (!elf_file) elf_file = LoadAndCacheElfFile(name);
  // Return an empty shared_ptr if no ELF file could be loaded.
  if (!elf_file) return {};

  // Increase a reference count to the ELF file.
  elf_file->IncrementInstances();
  return elf_file;
}

void DecrementElfFile(std::shared_ptr<ElfFile> elf_file) {
  // Decrease a reference count to the ELF file.
  elf_file->DecrementInstances();
  // Only continue if there are no more references.
  if (elf_file->AreThereStillReferences()) return;

  // Remove this ELF file from the cache by name.
  auto range_by_name = elf_files_by_name.equal_range(elf_file->File().Name());
  for (auto it = range_by_name.first; it != range_by_name.second; ++it) {
    if (it->second == elf_file) {
      elf_files_by_name.erase(it);
      break;
    }
  }

  // Remove this ELF file from the cache by path.
  auto range_by_path = elf_files_by_path.equal_range(elf_file->File().Path());
  for (auto it = range_by_path.first; it != range_by_path.second; ++it) {
    if (it->second == elf_file) {
      elf_files_by_path.erase(it);
      break;
    }
  }
}