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

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "elf.h"
#include "file.h"
#include "perception/shared_memory.h"
#include "status.h"

namespace perception {
class MemorySpan;
}

class ElfFile {
 public:
  ElfFile(std::unique_ptr<class File> file);

  const class File& File() const;

  // Whether this is a valid ELF file.
  bool IsValid() const;

  // Whether this is an executable file, and  not a library.
  bool IsExecutable();

  size_t EntryAddress(size_t offset);

  void ForEachDependentLibrary(
      const std::function<void(std::string_view)>& on_each);

  StatusOr<size_t> LoadIntoAddressSpaceAndReturnNextFreeAddress(
      ::perception::ProcessId child_pid, size_t offset,
      std::map<size_t, void*>& child_memory_pages,
      std::map<std::string, size_t>& symbols_to_addresses,
      std::vector<std::pair<size_t, size_t>>& preinit_arrays,
      std::vector<std::pair<size_t, size_t>>& init_functions,
      std::vector<std::pair<size_t, size_t>>& init_arrays,
      std::vector<std::pair<size_t, size_t>>& fini_functions,
      std::vector<std::pair<size_t, size_t>>& fini_arrays);

  ::perception::Status FixUpRelocations(
      std::map<size_t, void*>& child_memory_pages, size_t offset,
      const std::map<std::string, size_t>& symbols_to_addresses,
      size_t module_id);

  void IncrementInstances();
  void DecrementInstances();
  bool AreThereStillReferences() const;

 private:
  const Elf64_Ehdr* ElfHeader();
  std::span<const Elf64_Shdr> SectionHeaders();
  std::span<const Elf64_Phdr> ProgramSegmentHeaders();
  const char* SectionHeaderString(int index);
  const char* DynamicString(int index);
  const char* String(int index);

  void FindInterestingSections();
  bool CalculateLowestAndHighestVirtualAddresses();
  bool LoadSharedMemorySegments();

  std::vector<const Elf64_Shdr*> GetRelocationSectionHeaders();

  std::unique_ptr<class File> file_;

  // Whether the elf file is valid.
  bool is_valid_;

  // Instances that are running.
  size_t instances_;

  ::perception::MemorySpan memory_span_;
  size_t number_of_sections_;
  size_t number_of_program_segments_;
  ::perception::MemorySpan section_header_string_table_;
  ::perception::MemorySpan dynamic_string_table_;
  ::perception::MemorySpan string_table_;

  std::optional<const Elf64_Shdr*> got_section_header_;
  std::optional<const Elf64_Shdr*> got_plt_section_header_;
  std::optional<const Elf64_Shdr*> dynamic_section_header_;
  std::optional<const Elf64_Shdr*> rela_dyn_section_header_;
  std::optional<const Elf64_Shdr*> rela_plt_section_header_;
  std::optional<const Elf64_Shdr*> dynsym_section_header_;
  std::optional<const Elf64_Shdr*> preinit_array_section_header_;
  std::optional<const Elf64_Shdr*> init_section_header_;
  std::optional<const Elf64_Shdr*> init_array_section_header_;
  std::optional<const Elf64_Shdr*> fini_array_section_header_;
  std::optional<const Elf64_Shdr*> fini_section_header_;

  std::map<size_t, std::shared_ptr<perception::SharedMemory>>
      read_only_segments_;

  size_t lowest_virtual_address_;
  size_t highest_virtual_address_;  // Exclusive.
};
