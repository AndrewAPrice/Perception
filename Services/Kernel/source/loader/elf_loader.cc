// Copyright 2020 Google LLC
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

#include "loader/elf_loader.h"

#include "../../../Libraries/perception/public/perception/tracing.h"
#include "../../../third_party/Libraries/elf/public/elf.h"
#include "common/kernel_string.h"
#include "memory/memory.h"
#include "loader/multiboot_modules.h"
#include "memory/physical_allocator.h"
#include "processes/process.h"
#include "scheduling/scheduler.h"
#include "output/text_terminal.h"
#include "scheduling/thread.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"

namespace loader {

using memory::CopyKernelMemoryIntoProcess;
using memory::kPageSize;
using memory::kVirtualMemoryOffset;
using memory::TemporarilyMapPhysicalPages;
using memory::VirtualAddressSpace;
using memory::ZeroProcessMemory;
using output::print;
using processes::CreateProcess;
using processes::DestroyProcess;
using processes::kProcessNameLength;
using processes::Process;
using scheduling::CreateThread;
using scheduling::ScheduleThread;
using scheduling::Thread;

namespace {

// ELF auxiliary vector types.
constexpr size_t kAtNull = 0;
constexpr size_t kAtPhdr = 3;
constexpr size_t kAtPhnum = 4;
constexpr size_t kAtPhent = 5;
constexpr size_t kAtPagesz = 6;

// Is this a valid ELF header?
bool IsValidElfHeader(const Elf64_Ehdr* header, size_t file_size) {
  if (header == nullptr || file_size < sizeof(Elf64_Ehdr)) return false;

  if (header->e_ident[EI_MAG0] != ELFMAG0 ||
      header->e_ident[EI_MAG1] != ELFMAG1 ||
      header->e_ident[EI_MAG2] != ELFMAG2 ||
      header->e_ident[EI_MAG3] != ELFMAG3)
    return false;  // Invalid ELF header.

  if (header->e_ident[EI_CLASS] != ELFCLASS64)
    return false;  // Not a 64-bit ELF header.

  if (header->e_ident[EI_DATA] != ELFDATA2LSB)
    return false;  // Not little endian.

  if (header->e_ident[EI_VERSION] != EV_CURRENT)
    return false;  // Not the correct ELF header version.

  if (header->e_type != ET_EXEC)
    return false;  // Not an executable file.

  if (header->e_machine != EM_X86_64)
    return false;  // Not an X86_64 binary.

  if (header->e_phentsize != sizeof(Elf64_Phdr)) return false;
  if (header->e_phoff > file_size) return false;
  if (header->e_shoff > file_size) return false;
  if (header->e_phnum != PN_XNUM) {
    if (header->e_phnum * sizeof(Elf64_Phdr) > file_size - header->e_phoff)
      return false;
  }

  return true;
}

// Figures out the number of segments in the binary.
size_t GetNumberOfSegments(const Elf64_Ehdr* header, size_t memory_start,
                           size_t memory_end) {
  if (header == nullptr) return 0;
  if (header->e_phnum == PN_XNUM) {
    // The number of program headers is too large to fit into e_phnum. Instead,
    // it's found in the field sh_info of section 0.
    Elf64_Shdr* section_header = (Elf64_Shdr*)(memory_start + header->e_shoff);
    if ((size_t)section_header + sizeof(Elf64_Shdr) > memory_end) {
      print << "ELF not big enough for section.\n";
      return 0;
    }

    return section_header->sh_info;
  } else {
    return header->e_phnum;
  }
}
// Returns whether the ELF executable requires dynamic linking.
bool RequiresDynamicLinking(const Elf64_Ehdr* header, size_t memory_start,
                            size_t memory_end) {
  // Figure out the number of segments in the binary.
  size_t number_of_segments =
      GetNumberOfSegments(header, memory_start, memory_end);

  // Loop through the segments to see if there is a dynamic section.
  Elf64_Phdr* segment_header = (Elf64_Phdr*)(memory_start + header->e_phoff);
  for (int i = 0; i < number_of_segments; i++, segment_header++) {
    if ((size_t)segment_header + sizeof(Elf64_Phdr) > memory_end) {
      print << "ELF not big enough for segment.\n";
      return false;
    }

    if (segment_header->p_type == PT_DYNAMIC) {
      // Found a dynamical section, which means the binary requires dynamic
      // linking.
      return true;
    }
  }
  return false;
}

bool LoadSegments(const Elf64_Ehdr* header, size_t memory_start,
                  size_t memory_end, Process* process) {
  // Figure out the number of segments in the binary.
  size_t number_of_segments =
      GetNumberOfSegments(header, memory_start, memory_end);

  // Load the segments.
  Elf64_Phdr* segment_header = (Elf64_Phdr*)(memory_start + header->e_phoff);
  for (int i = 0; i < number_of_segments; i++, segment_header++) {
    if ((size_t)segment_header + sizeof(Elf64_Phdr) > memory_end) {
      print << "ELF not big enough for segment.\n";
      return false;
    }

    if (segment_header->p_type != PT_LOAD)
      continue;

    // Validate memory size and virtual address range
    if (segment_header->p_vaddr >= kVirtualMemoryOffset ||
        segment_header->p_memsz > kVirtualMemoryOffset ||
        segment_header->p_vaddr + segment_header->p_memsz >
            kVirtualMemoryOffset ||
        segment_header->p_vaddr + segment_header->p_memsz <
            segment_header->p_vaddr) {
      print << "Trying to load data into kernel memory or invalid segment "
               "size.\n";
      return false;
    }

    // Validate that file size is not greater than memory size
    if (segment_header->p_filesz > segment_header->p_memsz) {
      print << "Segment file size is greater than memory size.\n";
      return false;
    }

    if (segment_header->p_filesz > 0) {
      // There is data from the file to copy into memory.
      size_t from_start = memory_start + segment_header->p_offset;
      size_t from_size = segment_header->p_filesz;

      if (from_start + from_size > memory_end ||
          from_start + from_size < from_start) {
        // Segment is out of bounds of the ELF file or overflows.
        print << "Segment is trying to load memory that is out of bounds of "
                 "the file.\n";
        return false;
      }

      size_t to_address = segment_header->p_vaddr;
      size_t to_end = to_address + from_size;
      // Copy the data from the file into memory.
      if (!CopyKernelMemoryIntoProcess(from_start, to_address, to_end,
                                       process))
        return false;
    }

    if (segment_header->p_memsz > segment_header->p_filesz) {
      // This is memory that takes up no space in the ELF file, but must
      // be initialized to 0 for the program.

      // Skip over any data that was copied.
      size_t to_start = segment_header->p_vaddr + segment_header->p_filesz;
      size_t to_end =
          to_start + (segment_header->p_memsz - segment_header->p_filesz);
      if (!ZeroProcessMemory(to_start, to_end, process))
        return false;
    }
  }

  return true;
}

size_t CreateArgsPageForProcess(Process* process, const char* name, size_t phdr_addr, size_t phnum, size_t phent) {
  VirtualAddressSpace& address_space = process->virtual_address_space;
  size_t args_virtual_page = address_space.AllocatePages(1);
  if (args_virtual_page == kOutOfMemory) return 0;

  size_t physical_page = address_space.GetPhysicalAddress(args_virtual_page, false);
  if (physical_page == kOutOfMemory) {
    address_space.ReleasePages(args_virtual_page, 1);
    return 0;
  }

  char* temp_addr = (char*)TemporarilyMapPhysicalPages(physical_page, 1);
  memset(temp_addr, 0, kPageSize);

  // Single argument: the name. So argc = 1.
  size_t argc = 1;
  *(size_t*)temp_addr = argc;

  size_t pointers_offset = 8;
  size_t strings_offset = pointers_offset + 8 * (argc + 12); // Space for argv, envp, auxv

  // argv[0] = name
  size_t child_string_address = args_virtual_page + strings_offset;
  *(size_t*)(temp_addr + pointers_offset) = child_string_address;
  
  size_t name_len = strlen(name);
  if (strings_offset + name_len + 1 <= kPageSize) {
    memcpy(temp_addr + strings_offset, name, name_len);
    temp_addr[strings_offset + name_len] = '\0';
  }

  // envp[0] = NULL is at pointers_offset + 8 * (argc + 1), which is already 0.

  // auxv is at pointers_offset + 8 * (argc + 2)
  size_t write_auxv_offset = pointers_offset + 8 * (argc + 2);
  auto write_aux = [&](size_t type, size_t value) {
    *(size_t*)(temp_addr + write_auxv_offset) = type;
    *(size_t*)(temp_addr + write_auxv_offset + 8) = value;
    write_auxv_offset += 16;
  };

  write_aux(kAtPhdr, phdr_addr);
  write_aux(kAtPhnum, phnum);
  write_aux(kAtPhent, phent);
  write_aux(kAtPagesz, kPageSize);
  write_aux(kAtNull, 0);

  return args_virtual_page;
}

}  // namespace

bool LoadElfProcess(size_t memory_start, size_t memory_end, char* name) {
  char* original_name = name;
  size_t name_length = 0;
  bool is_driver = false;
  bool can_create_processes = false;
  bool can_set_focus = false;

  if (!ParseMultibootModuleName(name, name_length, is_driver,
                                can_create_processes, can_set_focus)) {
    print << "Can't load module \"" << original_name
          << "\" because the name is not in the correct format.\n";
    // Even though the module didn't do anything, it can't be loaded later
    // because the name is invalid. Returning false here would cause it to be
    // picked up as a module that can be sent to a process, but it can't be.
    return true;
  }

  if (memory_start + sizeof(Elf64_Ehdr) > memory_end) {
    return false;
  }

  Elf64_Ehdr* header = (Elf64_Ehdr*)memory_start;
  if (!IsValidElfHeader(header, memory_end - memory_start)) {
    // Not an ELF file. This is fine - this module can be sent to a process
    // later to see if it can handle it.
    return false;
  }

  if (RequiresDynamicLinking(header, memory_start, memory_end)) {
    // ELF files that require dynamic linking can't be loaded by the kernel.
    return false;
  }

  if (is_driver) {
    print << "Loading driver ";
  } else {
    print << "Loading application ";
  }

  print << name << "...\n";

  Process* process =
      CreateProcess(is_driver, can_create_processes, can_set_focus);
  if (!process) {
    print << "Can't load: " << name
          << ": Out of memory to create the process.\n";
    return false;
  }

  common::CopyString(name, kProcessNameLength, name_length,
                     (char*)process->name);
#ifdef ENABLE_TRACING
  EmitProcessCreatedTrace(process);
#endif

  size_t phnum = GetNumberOfSegments(header, memory_start, memory_end);
  size_t phent = header->e_phentsize;
  size_t phdr_addr = 0;

  Elf64_Phdr* segment_header = (Elf64_Phdr*)(memory_start + header->e_phoff);
  for (int i = 0; i < phnum; i++, segment_header++) {
    if ((size_t)segment_header + sizeof(Elf64_Phdr) > memory_end) {
      break;
    }
    if (segment_header->p_type == PT_PHDR) {
      phdr_addr = segment_header->p_vaddr;
      break;
    }
  }
  if (phdr_addr == 0) {
    segment_header = (Elf64_Phdr*)(memory_start + header->e_phoff);
    for (int i = 0; i < phnum; i++, segment_header++) {
      if ((size_t)segment_header + sizeof(Elf64_Phdr) > memory_end) {
        break;
      }
      if (segment_header->p_type == PT_LOAD && segment_header->p_offset == 0) {
        phdr_addr = segment_header->p_vaddr + header->e_phoff;
        break;
      }
    }
  }

  if (!LoadSegments(header, memory_start, memory_end, process)) {
    print << "Can't load: " << name << ": Segments are bad.\n";
    DestroyProcess(process);
    return false;
  }

  size_t args_address = CreateArgsPageForProcess(process, name, phdr_addr, phnum, phent);

  Thread* thread = CreateThread(process, header->e_entry, args_address);
  if (!thread) {
    print << "Can't load: " << name
          << ": Out of memory to create the thread.\n";
    DestroyProcess(process);
    return false;
  }

  ScheduleThread(thread);
  return true;
}

}  // namespace loader
