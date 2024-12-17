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
#include "elf_header.h"

#include <iostream>

#include "elf.h"
#include "perception/memory_span.h"

using ::perception::MemorySpan;

bool IsValidElfHeader(const Elf64_Ehdr& header) {
  if (header.e_ident[EI_MAG0] != ELFMAG0 ||
      header.e_ident[EI_MAG1] != ELFMAG1 ||
      header.e_ident[EI_MAG2] != ELFMAG2 ||
      header.e_ident[EI_MAG3] != ELFMAG3) {
    std::cout << "Invalid ELF header." << std::endl;
    return false;
  }

  if (header.e_ident[EI_CLASS] != ELFCLASS64) {
    std::cout << "Not a 64-bit ELF header." << std::endl;
    return false;
  }

  if (header.e_ident[EI_DATA] != ELFDATA2LSB) {
    std::cout << "Not little endian." << std::endl;
    return false;
  }

  if (header.e_ident[EI_VERSION] != EV_CURRENT) {
    std::cout << "Not an ELF header version." << std::endl;
    return false;
  }

  if (header.e_type != ET_EXEC && header.e_type != ET_DYN) {
    std::cout << "Not an executable file or a shared library." << std::endl;
    return false;
  }

  if (header.e_machine != EM_X86_64) {
    std::cout << "Not an X86_64 binary." << std::endl;
    return false;
  }

  return true;
}

bool IsValidElfFile(const MemorySpan& file) {
  const Elf64_Ehdr* header = file.ToType<Elf64_Ehdr>();
  return header && IsValidElfHeader(*header);
}
