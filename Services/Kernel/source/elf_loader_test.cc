// Copyright 2026 Google LLC
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

#include "elf_loader.h"

#include <stddef.h>

#include <unordered_map>

#include "../../../third_party/Libraries/elf/public/elf.h"
#include "kernel_string.h"
#include "object_pools.h"
#include "process.h"
#include "testing.h"
#include "thread.h"
#include "virtual_allocator.h"

struct PhysicalPageBuffer {
  size_t entries[512];
};

extern std::unordered_map<size_t, PhysicalPageBuffer> simulated_ram;

struct MockElf {
  Elf64_Ehdr header;
  Elf64_Phdr phdr;
  char program_data[32];
};

TEST(ElfLoaderLoadProcessTest) {
  InitializeObjectPools();
  InitializeProcesses();
  InitializeThreads();
  InitializeVirtualAllocator();

  MockElf elf;

  // Set up ELF64 Header
  elf.header.e_ident[EI_MAG0] = ELFMAG0;
  elf.header.e_ident[EI_MAG1] = ELFMAG1;
  elf.header.e_ident[EI_MAG2] = ELFMAG2;
  elf.header.e_ident[EI_MAG3] = ELFMAG3;
  elf.header.e_ident[EI_CLASS] = ELFCLASS64;
  elf.header.e_ident[EI_DATA] = ELFDATA2LSB;
  elf.header.e_ident[EI_VERSION] = EV_CURRENT;

  elf.header.e_type = ET_EXEC;
  elf.header.e_machine = EM_X86_64;
  elf.header.e_version = EV_CURRENT;
  elf.header.e_entry = 0x1000;  // Entry point
  elf.header.e_phoff = offsetof(MockElf, phdr);
  elf.header.e_shoff = 0;
  elf.header.e_flags = 0;
  elf.header.e_ehsize = sizeof(Elf64_Ehdr);
  elf.header.e_phentsize = sizeof(Elf64_Phdr);
  elf.header.e_phnum = 1;
  elf.header.e_shentsize = 0;
  elf.header.e_shnum = 0;
  elf.header.e_shstrndx = SHN_UNDEF;

  // Program Segment Header
  elf.phdr.p_type = PT_LOAD;
  elf.phdr.p_flags = PF_R | PF_X;
  elf.phdr.p_offset = offsetof(MockElf, program_data);
  elf.phdr.p_vaddr = 0x400000;  // load virtual address
  elf.phdr.p_paddr = 0x400000;
  elf.phdr.p_filesz = 32;
  elf.phdr.p_memsz = 32;
  elf.phdr.p_align = 4096;

  // Data segment payload
  const char* dummy_instr = "Dummy ELF Code!";
  for (int i = 0; i < 32; i++) {
    elf.program_data[i] = i < 16 ? dummy_instr[i] : '\0';
  }

  // Load the mock ELF process
  bool success = LoadElfProcess((size_t)&elf, (size_t)&elf + sizeof(MockElf),
                                (char*)"- my_process");
  ASSERT(success, true);

  // Verify process was created successfully
  char query_name[PROCESS_NAME_LENGTH + 1];
  CopyString("my_process", PROCESS_NAME_LENGTH, 10, query_name);
  Process* process = FindNextProcessWithName(query_name, nullptr);
  ASSERT(process != nullptr, true);

  int match = 1;
  const char* expected = "my_process";
  for (int i = 0; i < 10; i++) {
    if (process->name[i] != expected[i]) {
      match = 0;
      break;
    }
  }
  ASSERT(match, 1);

  // Verify virtual memory mappings and data integrity at 0x400000
  size_t phys_page =
      process->virtual_address_space.GetPhysicalAddress(0x400000, false);
  ASSERT(phys_page != OUT_OF_MEMORY, true);

  char* loaded_data = (char*)simulated_ram[phys_page].entries;
  int data_match = 1;
  for (int i = 0; i < 16; i++) {
    if (loaded_data[i] != dummy_instr[i]) {
      data_match = 0;
      break;
    }
  }
  ASSERT(data_match, 1);

  // Verify the process's first thread entry point matches 0x1000
  ASSERT(process->thread_count, (unsigned short)1);
  Thread* thread = process->threads.FirstItem();
  ASSERT(thread != nullptr, true);
  ASSERT(thread->registers.rip, (size_t)0x1000);

  // Clean up
  DestroyProcess(process);
}
