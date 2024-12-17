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

class InitFiniFunctions;

// An ELF file, which may either be an executable or a shared library.
class ElfFile {
 public:
  // Construct an ELF file from an underlying file.
  ElfFile(std::unique_ptr<class File> file);

  // Returns the underlying file.
  const class File& File() const;

  // Whether this is a valid ELF file.
  bool IsValid() const;

  // Whether this is an executable file, and not a library.
  bool IsExecutable();

  // Returns the entry address (with the given offset added). This is only valid
  // for executables.
  size_t EntryAddress(size_t offset);

  // Calls `on_each` with the name of each dependent shared library this ELF
  // file depends on.
  void ForEachDependentLibrary(
      const std::function<void(std::string_view)>& on_each);

  // Loads this ELF file into a child process at the provided memory `offset`,
  // and if successful, returns the next free address. Any read-only shared
  // memory segments will be mapped into the child process.
  // `child_memory_pages` will be populated with unique writable memory.
  // `symbols_to_addresses` will be populated with exported symbols.
  // `init_fini_functions` will be populated with initializer and finalizer
  // functions.
  StatusOr<size_t> LoadIntoAddressSpaceAndReturnNextFreeAddress(
      ::perception::ProcessId child_pid, size_t offset,
      std::map<size_t, void*>& child_memory_pages,
      std::map<std::string, size_t>& symbols_to_addresses,
      InitFiniFunctions& init_fini_functions);

  // Fix up relocations in a child process for this ELF file after the
  // executable and all dependent libraries have been loaded into the child
  // process. `child_memory_pages` contains the unique writable memory for this
  // process. `offset` is the same memory offset that this ELF file was loaded
  // at in thils child process. `symbols_to_addresses` contains all of the
  // exported symbols throughout the child process. `module_id` is a unique ID
  // representing this ELF file in this process.
  ::perception::Status FixUpRelocations(
      std::map<size_t, void*>& child_memory_pages, size_t offset,
      const std::map<std::string, size_t>& symbols_to_addresses,
      size_t module_id);

  // Increments a reference count for this ELF file.
  void IncrementInstances();

  // Decrements a reference count for this ELF file. This alone does nothing if
  // it reaches 0, so this should be called though `DecrementElfFile` in
  // elf_file_cache.h.
  void DecrementInstances();

  // Returns whether there is at least 1 reference to this ELF file.
  bool AreThereStillReferences() const;

 private:
  // Returns a pointer to the ELF header, or nullptr if it's not valid.
  const Elf64_Ehdr* ElfHeader();

  // Returns a list of section headers.
  std::span<const Elf64_Shdr> SectionHeaders();

  // Returns a list of program segment headers.
  std::span<const Elf64_Phdr> ProgramSegmentHeaders();

  // Returns a section header string from an index, or nullptr.
  const char* SectionHeaderString(int index);

  // Returns a dynamic string from an index, or nullptr.
  const char* DynamicString(int index);

  // Scans the section headers and records the interesting sections for later.
  void FindInterestingSections();

  // Calculates the highest known virtual address (exclusive) in this ELF file.
  void CalculateHighestVirtualAddresses();

  // Create shared memory blocks for the read only segments that can be shared
  // between multiple instances of processes referring to this ELF file.
  bool CreateSharedMemorySegments();

  // Adds the initializer and finializer functions to `init_fini_functions` for
  // when this ELF file is loaded into a child at the provided memory `offset`.
  void AddToInitFiniFunctions(size_t offset,
                              InitFiniFunctions& init_fini_functions);

  // Returns the headers containing relocation information that needs to be
  // fixed up whenever this ELF file is loaded into a child process.
  std::vector<const Elf64_Shdr*> GetRelocationSectionHeaders();

  // The underlying file containing the ELF data.
  std::unique_ptr<class File> file_;

  // Whether the elf file is valid.
  bool is_valid_;

  // How many references there are to this ELF file..
  size_t instances_;

  // Memory span around the `file_`.
  ::perception::MemorySpan memory_span_;

  // The number of sections in this ELF file.
  size_t number_of_sections_;

  // The number of program segments in this ELF file.
  size_t number_of_program_segments_;

  // The section header string table. This is a pointer inside of the file
  // loaded into memory.
  ::perception::MemorySpan section_header_string_table_;

  // The dynamic string table. This is a pointer inside of the file loaded into
  // memory.
  ::perception::MemorySpan dynamic_string_table_;

  // Pointers to various interesting ELF sections. These are pointers inside of
  // the file loaded into memory.
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

  // The read only segments to load into child processes. This is a set of
  // SharedMemory blocks to map into the child process and the non-offsetted
  // virtual address to map them at.
  std::map<size_t, std::shared_ptr<perception::SharedMemory>>
      read_only_segments_;

  // The highest known virtual address this ELF file references to. Exclusive.
  size_t highest_virtual_address_;
};
