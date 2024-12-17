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

#include "elf_file.h"

#include <iostream>
#include <memory>
#include <span>

#include "elf.h"
#include "elf_header.h"
#include "memory.h"
#include "perception/memory.h"
#include "perception/memory_span.h"

using ::perception::kPageSize;
using ::perception::MemorySpan;
using ::perception::Status;

namespace {

void MaybeAddSectionHeaderArrayToArrays(
    std::optional<const Elf64_Shdr*> array_section_header, size_t offset,
    std::vector<std::pair<size_t, size_t>>& arrays) {
  if (array_section_header) {
    arrays.push_back({(*array_section_header)->sh_addr + offset,
                      (*array_section_header)->sh_size / sizeof(size_t)});
  }
}

void MaybeAddSectionHeaderFunctionToCodeArray(
    std::optional<const Elf64_Shdr*> function_section_header, size_t offset,
    std::vector<std::pair<size_t, size_t>>& array) {
  if (function_section_header) {
    array.push_back({(*function_section_header)->sh_addr + offset,
                     (*function_section_header)->sh_size});
  }
}

}  // namespace

ElfFile::ElfFile(std::unique_ptr<class File> file)
    : file_(std::move(file)), instances_(0) {
  is_valid_ = false;
  memory_span_ = file_->MemorySpan();

  // Make sure the file has a valid ELF header.
  auto header = ElfHeader();
  if (!header & !IsValidElfHeader(*header)) return;

  // Make sure there are valid section headers in the file.
  number_of_sections_ = header->e_shnum;
  auto section_headers = SectionHeaders();
  if (number_of_sections_ == 0 || section_headers.size() == 0) return;

  number_of_program_segments_ = header->e_phnum;
  if (number_of_program_segments_ == PN_XNUM) {
    // The number of program headers is too large to fit into e_phnum. Instead,
    // it's found in the field sh_info of section 0.
    number_of_program_segments_ = section_headers[0].sh_info;
  }

  // Make sure there are valid program segment headers in the file.
  auto program_segment_headers = ProgramSegmentHeaders();
  if (number_of_program_segments_ == 0 || program_segment_headers.size() == 0)
    return;

  size_t section_header_string_table_section_index = header->e_shstrndx;
  if (section_header_string_table_section_index < number_of_sections_) {
    auto& section_header =
        section_headers[section_header_string_table_section_index];
    section_header_string_table_ =
        memory_span_.SubSpan(section_header.sh_offset, section_header.sh_size);
  }

  FindInterestingSections();
  if (!CalculateLowestAndHighestVirtualAddresses()) return;
  if (!LoadSharedMemorySegments()) return;

  is_valid_ = true;
}

const class File& ElfFile::File() const { return *file_; }

bool ElfFile::IsValid() const { return is_valid_; }

bool ElfFile::IsExecutable() {
  auto header = ElfHeader();
  if (header == nullptr) {
    std::cout << "Header is null.";
    return false;
  }
  return header->e_type == ET_EXEC;
}

size_t ElfFile::EntryAddress(size_t offset) {
  auto header = ElfHeader();
  if (header == nullptr) {
    std::cout << "Header is null.";
    return 0;
  }

  return header->e_entry + offset;  // offset;
}

void ElfFile::ForEachDependentLibrary(
    const std::function<void(std::string_view)>& on_each) {
  if (dynamic_section_header_) {
    auto dynamic_entries = memory_span_.ToTypedArrayAtOffset<Elf64_Dyn>(
        (*dynamic_section_header_)->sh_offset,
        (*dynamic_section_header_)->sh_size / sizeof(Elf64_Dyn));
    for (const Elf64_Dyn& dynamic_entry : dynamic_entries) {
      if (dynamic_entry.d_tag != DT_NEEDED) continue;

      const char* str = DynamicString(dynamic_entry.d_un.d_val);
      if (str == nullptr) continue;
      on_each(std::string_view(str, strlen(str)));
    }
  }
}

StatusOr<size_t> ElfFile::LoadIntoAddressSpaceAndReturnNextFreeAddress(
    ::perception::ProcessId child_pid, size_t offset,
    std::map<size_t, void*>& child_memory_pages,
    std::map<std::string, size_t>& symbols_to_addresses,
    std::vector<std::pair<size_t, size_t>>& preinit_arrays,
    std::vector<std::pair<size_t, size_t>>& init_functions,
    std::vector<std::pair<size_t, size_t>>& init_arrays,
    std::vector<std::pair<size_t, size_t>>& fini_functions,
    std::vector<std::pair<size_t, size_t>>& fini_arrays) {
  // Load shared memory segments.
  for (const auto& address_and_shared_memory : read_only_segments_) {
    size_t address = address_and_shared_memory.first + offset;
    if (!address_and_shared_memory.second->JoinChildProcess(child_pid,
                                                            address)) {
      std::cout << "Unable to join a child process into shared memory at: "
                << std::hex << address << std::dec << std::endl;
      return Status::INTERNAL_ERROR;
    }
  }

  // Load non shared memory segments.
  for (const auto& segment_header : ProgramSegmentHeaders()) {
    if (segment_header.p_type != PT_LOAD)
      continue;  // Segment doesn't get loaded.
    if ((segment_header.p_flags & PF_W) == 0)
      continue;  // Segment isn't writable.

    if (segment_header.p_filesz > 0) {
      // There is data from the file to copy into memory.
      const void* data = *memory_span_.SubSpan(segment_header.p_offset,
                                               segment_header.p_filesz);
      if (data == nullptr) {
        std::cout << "Segment is trying to load memory that is out of bounds "
                     "of the file."
                  << std::endl;
        return false;
      }

      size_t address = segment_header.p_vaddr + offset;
      // Copy the data from the file into memory.
      if (!CopyIntoMemory(data, segment_header.p_filesz, address,
                          child_memory_pages)) {
        return false;
      }
    }

    if (segment_header.p_memsz > segment_header.p_filesz) {
      // This is memory that takes up no space in the ELF file, but must
      // be initialized to 0 for the program.

      // SKip over any data that was copied.
      size_t address =
          segment_header.p_vaddr + segment_header.p_filesz + offset;
      size_t size = segment_header.p_memsz - segment_header.p_filesz;
      if (!LoadMemory(address, size, child_memory_pages)) {
        return false;
      }
    }
  }

  // Add exported symbols.
  if (dynsym_section_header_) {
    // Skip the first symbol entry, as it's the a special entry for "undefined"
    // symbols.
    auto symbols = memory_span_.ToTypedArrayAtOffset<Elf64_Sym>(
        (*dynsym_section_header_)->sh_offset + sizeof(Elf64_Sym),
        (*dynsym_section_header_)->sh_size / sizeof(Elf64_Sym) - 1);

    // auto section_headers = SectionHeaders();
    for (const Elf64_Sym& symbol : symbols) {
      if (symbol.st_shndx == SHN_UNDEF) continue;  // Undefined symbol.
      if (ELF64_ST_BIND(symbol.st_info) == STB_LOCAL)
        continue;  // Skip local symbols.

      std::string name = DynamicString(symbol.st_name);
      bool is_weak = ELF64_ST_BIND(symbol.st_info) == STB_WEAK;
      if (!is_weak || !symbols_to_addresses.contains(name)) {
        size_t address = symbol.st_value + offset;
        symbols_to_addresses[name] = address;
      }
    }
  }

  MaybeAddSectionHeaderArrayToArrays(preinit_array_section_header_, offset,
                                     preinit_arrays);
  MaybeAddSectionHeaderArrayToArrays(init_array_section_header_, offset,
                                     init_arrays);
  MaybeAddSectionHeaderArrayToArrays(fini_array_section_header_, offset,
                                     fini_arrays);
  MaybeAddSectionHeaderFunctionToCodeArray(init_section_header_, offset,
                                           init_functions);
  MaybeAddSectionHeaderFunctionToCodeArray(fini_section_header_, offset,
                                           fini_functions);

  // Return the highest virtual memory address.
  return highest_virtual_address_ + offset;
}

Status ElfFile::FixUpRelocations(
    std::map<size_t, void*>& child_memory_pages, size_t offset,
    const std::map<std::string, size_t>& symbols_to_addresses,
    size_t module_id) {
  auto relocation_section_headers = GetRelocationSectionHeaders();
  if (relocation_section_headers.empty()) return Status::OK;

  auto symbols = memory_span_.ToTypedArrayAtOffset<Elf64_Sym>(
      (*dynsym_section_header_)->sh_offset,
      (*dynsym_section_header_)->sh_size / sizeof(Elf64_Sym));
  for (const auto* relocation_section_header : relocation_section_headers) {
    auto relocation_entries = memory_span_.ToTypedArrayAtOffset<Elf64_Rela>(
        relocation_section_header->sh_offset,
        relocation_section_header->sh_size / sizeof(Elf64_Rela));

    for (const auto& relocation_entry : relocation_entries) {
      size_t type = ELF64_R_TYPE(relocation_entry.r_info);
      size_t value = 0;

      switch (type) {
        case 1:    // R_AMD64_64 - Symbol value + addend.
        case 6:    // R_AMD64_GLOB_DAT - Symbol value.
        case 7: {  // R_AMD64_JUMP_SLOT
          size_t symbol_index = ELF64_R_SYM(relocation_entry.r_info);
          if (symbol_index >= symbols.size()) {
            std::cout << "Symbol index " << symbol_index
                      << " is out of range: " << symbols.size() << std::endl;
            return Status::INTERNAL_ERROR;
          }

          const auto& symbol = symbols[symbol_index];

          if (symbol.st_shndx == SHN_UNDEF) {
            // The symbol is not defined in this image, so it needs to be
            // resolved from another image.
            std::string name = DynamicString(symbol.st_name);
            auto itr = symbols_to_addresses.find(name);
            if (itr == symbols_to_addresses.end()) {
              if (ELF64_ST_BIND(symbol.st_info) == STB_WEAK) {
                // Missing weak symbols are fine.
                value = 0;
              } else {
                std::cout << "Cannot find needed symbol: " << name << std::endl;
                return Status::INVALID_ARGUMENT;
              }
            } else {
              value = itr->second;
            }
          } else {
            // The symbol is defined here.
            value = symbol.st_value + offset;
          }

          if (type == 1) value += relocation_entry.r_addend;
          break;
        }
        case 8:  // R_AMD64_RELATIVE
          value = relocation_entry.r_addend + offset;
          break;
        case 16:  // R_AMD64_DTPMOD64
          value = module_id;
          break;
        default:
          // Documentation on the different types are here:
          // https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-54839.html#gentextid-15318
          std::cout << "Unhandled relocation type: " << type << std::endl;
          return Status::UNIMPLEMENTED;
      }

      size_t address = relocation_entry.r_offset + offset;
      if (address % 8 != 0) {
        std::cout << "Relocation offset is not 64-bit aligned: " << address
                  << std::endl;
        return Status::INTERNAL_ERROR;
      }

      size_t page = address & ~(kPageSize - 1);
      size_t offset_in_page = address & (kPageSize - 1);

      auto page_itr = child_memory_pages.find(page);
      if (page_itr == child_memory_pages.end()) {
        std::cout << "Relocation offset is at an address that doesn't have "
                     "memory allocated to it: "
                  << std::hex << address << std::dec << std::endl;
        return Status::INTERNAL_ERROR;
      }

      ((size_t*)page_itr->second)[offset_in_page / 8] = value;
    }
  }

  return Status::OK;
}

void ElfFile::IncrementInstances() { instances_++; }

void ElfFile::DecrementInstances() { instances_--; }

bool ElfFile::AreThereStillReferences() const { return instances_ > 0; }

const Elf64_Ehdr* ElfFile::ElfHeader() {
  return memory_span_.ToType<Elf64_Ehdr>();
}

std::span<const Elf64_Shdr> ElfFile::SectionHeaders() {
  auto header = ElfHeader();
  if (header == nullptr) return std::span((const Elf64_Shdr*)nullptr, 0);
  return memory_span_.ToTypedArrayAtOffset<Elf64_Shdr>(header->e_shoff,
                                                       number_of_sections_);
}

std::span<const Elf64_Phdr> ElfFile::ProgramSegmentHeaders() {
  auto header = ElfHeader();
  if (header == nullptr) return std::span((const Elf64_Phdr*)nullptr, 0);
  return memory_span_.ToTypedArrayAtOffset<Elf64_Phdr>(
      header->e_phoff, number_of_program_segments_);
}

const char* ElfFile::SectionHeaderString(int index) {
  if (!section_header_string_table_) return nullptr;
  if (index >= section_header_string_table_.Length()) return nullptr;
  return &((const char*)*section_header_string_table_)[index];
}

const char* ElfFile::DynamicString(int index) {
  if (!dynamic_string_table_) return nullptr;
  if (index >= dynamic_string_table_.Length()) return nullptr;
  return &((const char*)*dynamic_string_table_)[index];
}

const char* ElfFile::String(int index) {
  if (!string_table_) return nullptr;
  if (index >= string_table_.Length()) return nullptr;
  return &((const char*)*string_table_)[index];
}

void ElfFile::FindInterestingSections() {
  for (const auto& section_header : SectionHeaders()) {
    /* std::cout << "Section in " << File().Name()
              << " name: " << SectionHeaderString(section_header.sh_name)
              << " size: " << std::dec << section_header.sh_size
              << " from: " << std::hex << section_header.sh_addr << " to "
              << (section_header.sh_offset + section_header.sh_addr)
              << std::endl; */
    const char* section_name = SectionHeaderString(section_header.sh_name);
    if (section_name == nullptr) continue;

    if (!strcmp(section_name, ".got")) {
      got_section_header_ = &section_header;
      //      std::cout << "Find GOT!\n";
    } else if (!strcmp(section_name, ".got.plt")) {
      got_plt_section_header_ = &section_header;
      //      std::cout << "Find GOT.PLT!\n";
    } else if (!strcmp(section_name, ".dynamic")) {
      dynamic_section_header_ = &section_header;
      //      std::cout << "Find DYNAMIC!\n";
    } else if (!strcmp(section_name, ".rela.dyn")) {
      rela_dyn_section_header_ = &section_header;
      //      std::cout << "Find RELA.DYN!\n";
    } else if (!strcmp(section_name, ".rela.plt")) {
      rela_plt_section_header_ = &section_header;
      //     std::cout << "Find RELA.PLT!\n";
    } else if (!strcmp(section_name, ".dynsym")) {
      dynsym_section_header_ = &section_header;
      //     std::cout << "Find DYNSYM!\n";
    } else if (!strcmp(section_name, ".strtab")) {
      string_table_ = memory_span_.SubSpan(section_header.sh_offset,
                                           section_header.sh_size);
    } else if (!strcmp(section_name, ".dynstr")) {
      dynamic_string_table_ = memory_span_.SubSpan(section_header.sh_offset,
                                                   section_header.sh_size);
    } else if (!strcmp(section_name, ".preinit_array")) {
      preinit_array_section_header_ = &section_header;
    } else if (!strcmp(section_name, ".init")) {
      std::cout << File().Name()
                << " contains an .init section instead of .init_array, and "
                   "support is flakey."
                << std::endl;
      init_section_header_ = &section_header;
    } else if (!strcmp(section_name, ".init_array")) {
      init_array_section_header_ = &section_header;
    } else if (!strcmp(section_name, ".fini")) {
      std::cout << File().Name()
                << " contains an .fini section instead of .init_array, and "
                   "support is flakey."
                << std::endl;
      fini_section_header_ = &section_header;
    } else if (!strcmp(section_name, ".fini_array")) {
      fini_array_section_header_ = &section_header;
    }
  }
}

bool ElfFile::CalculateLowestAndHighestVirtualAddresses() {
  lowest_virtual_address_ = 0xFFFFFFFFFFFFFFFF;
  highest_virtual_address_ = 0;

  for (const auto& segment_header : ProgramSegmentHeaders()) {
    if (segment_header.p_type != PT_LOAD)
      continue;  // Segment doesn't get loaded.
    lowest_virtual_address_ =
        std::min(segment_header.p_vaddr, lowest_virtual_address_);
    highest_virtual_address_ =
        std::max(segment_header.p_vaddr + segment_header.p_memsz,
                 highest_virtual_address_);
  }

  // Round lowest down to the nearest page.
  lowest_virtual_address_ = lowest_virtual_address_ & ~(kPageSize - 1);
  // Round highest up.
  highest_virtual_address_ =
      (highest_virtual_address_ + kPageSize - 1) & ~(kPageSize - 1);

  return lowest_virtual_address_ < highest_virtual_address_;
}

bool ElfFile::LoadSharedMemorySegments() {
  // These are readonly memory pages to assign to each child. This must be
  // cleaned up in all return paths.
  std::map<size_t, void*> child_memory_pages;

  for (const auto& segment_header : ProgramSegmentHeaders()) {
    if (segment_header.p_type != PT_LOAD)
      continue;  // Segment doesn't get loaded.
    if ((segment_header.p_flags & PF_W) != 0) continue;  // Segment is writable.

    if (segment_header.p_filesz > 0) {
      // There is data from the file to copy into memory.
      const void* data = *memory_span_.SubSpan(segment_header.p_offset,
                                               segment_header.p_filesz);
      if (data == nullptr) {
        std::cout << "Segment is trying to load memory that is out of bounds "
                     "of the file."
                  << std::endl;
        FreeChildMemoryPages(child_memory_pages);
        return false;
      }

      size_t address = segment_header.p_vaddr;
      // Copy the data from the file into memory.
      if (!CopyIntoMemory(data, segment_header.p_filesz, address,
                          child_memory_pages)) {
        FreeChildMemoryPages(child_memory_pages);
        return false;
      }
    }

    if (segment_header.p_memsz > segment_header.p_filesz) {
      // This is memory that takes up no space in the ELF file, but must
      // be initialized to 0 for the program.

      // SKip over any data that was copied.
      size_t address = segment_header.p_vaddr + segment_header.p_filesz;
      size_t size = segment_header.p_memsz - segment_header.p_filesz;
      if (!LoadMemory(address, size, child_memory_pages)) {
        FreeChildMemoryPages(child_memory_pages);
        return false;
      }
    }
  }

  read_only_segments_ =
      ConvertMapOfPagesIntoReadOnlySharedMemoryBlocks(child_memory_pages);

  return true;
}

std::vector<const Elf64_Shdr*> ElfFile::GetRelocationSectionHeaders() {
  if (!dynsym_section_header_)
    return {};  // Can't relocate without dynamic symbols.

  std::vector<const Elf64_Shdr*> relocation_sections;
  if (rela_dyn_section_header_)
    relocation_sections.push_back(*rela_dyn_section_header_);
  if (rela_plt_section_header_)
    relocation_sections.push_back(*rela_plt_section_header_);

  return relocation_sections;
}