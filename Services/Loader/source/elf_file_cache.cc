
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
#include <mutex>
#include <string_view>

#include "virtual_address_allocator.h"

namespace {

std::mutex cache_mutex;

// Loaded ELF files by name.
std::multimap<std::string_view, std::shared_ptr<ElfFile>, std::less<>>
    elf_files_by_name;

// Loaded ELF files by path.
std::multimap<std::string_view, std::shared_ptr<ElfFile>, std::less<>>
    elf_files_by_path;

// Returns an ELF file if it is cached. First checks by name, then by path.
// Returns an empty shared_ptr if no ELF file can be found.
// Assumes cache_mutex is held.
std::shared_ptr<ElfFile> GetCachedElfFile(std::string_view name) {
  auto itr_by_name = elf_files_by_name.find(name);
  if (itr_by_name != elf_files_by_name.end()) return itr_by_name->second;

  auto itr_by_path = elf_files_by_path.find(name);
  if (itr_by_path != elf_files_by_path.end()) return itr_by_path->second;

  return {};
}

// Loads an ELF file (by name or path) without holding cache_mutex. Returns an
// empty shared_ptr if the ELF file cannot be loaded.
std::shared_ptr<ElfFile> LoadElfFile(std::string_view name) {
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
  return elf_file;
}

}  // namespace

std::shared_ptr<ElfFile> LoadOrIncrementElfFile(std::string_view name) {
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (auto elf_file = GetCachedElfFile(name)) {
      elf_file->IncrementInstances();
      return elf_file;
    }
  }

  // Load the ELF file without holding cache_mutex across disk I/O.
  auto loaded_elf_file = LoadElfFile(name);
  if (!loaded_elf_file) return {};

  std::lock_guard<std::mutex> lock(cache_mutex);
  if (auto existing = GetCachedElfFile(name)) {
    existing->IncrementInstances();
    return existing;
  }

  elf_files_by_name.insert({loaded_elf_file->File().Name(), loaded_elf_file});
  elf_files_by_path.insert({loaded_elf_file->File().Path(), loaded_elf_file});
  loaded_elf_file->IncrementInstances();
  return loaded_elf_file;
}

void DecrementElfFile(std::shared_ptr<ElfFile> elf_file) {
  std::lock_guard<std::mutex> lock(cache_mutex);

  // Decrease a reference count to the ELF file.
  elf_file->DecrementInstances();
  // Only continue if there are no more references.
  if (elf_file->AreThereStillReferences()) return;

  if (elf_file->GetAssignedBaseAddress() != 0) {
    VirtualAddressAllocator::Get().FreeRange(elf_file->GetAssignedBaseAddress());
    elf_file->ClearAssignedBaseAddress();
  }

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