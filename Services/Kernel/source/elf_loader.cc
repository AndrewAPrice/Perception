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

#include "elf_loader.h"

#include "../../../third_party/Libraries/elf/public/elf.h"
#include "io.h"
#include "physical_allocator.h"
#include "process.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_allocator.h"

namespace {

// Is this a valid ELF header?
bool IsValidElfHeader(Elf64_Ehdr* header) {
  if (header->e_ident[EI_MAG0] != ELFMAG0 ||
      header->e_ident[EI_MAG1] != ELFMAG1 ||
      header->e_ident[EI_MAG2] != ELFMAG2 ||
      header->e_ident[EI_MAG3] != ELFMAG3) {
    print << "Invalid ELF header.";
    return false;
  }

  if (header->e_ident[EI_CLASS] != ELFCLASS64) {
    print << "Not a 64-bit ELF header.";
    return false;
  }

  if (header->e_ident[EI_DATA] != ELFDATA2LSB) {
    print << "Not little endian.";
    return false;
  }

  if (header->e_ident[EI_VERSION] != EV_CURRENT) {
    print << "Not an ELF header version.";
    return false;
  }

  if (header->e_type != ET_EXEC) {
    print << "Not an executable file.";
    return false;
  }

  if (header->e_machine != EM_X86_64) {
    print << "Not an X86_64 binary.";
    return false;
  }

  return true;
}

// Copies data from the module into the process's memory.
bool CopyIntoMemory(size_t from_start, size_t to_start, size_t to_end,
                    Process* process) {
  VirtualAddressSpace* address_space = &process->virtual_address_space;

  // The process's memory is mapped into pages. We'll copy page by page.
  size_t to_first_page = to_start & ~(PAGE_SIZE - 1);  // Round down.
  size_t to_last_page =
      (to_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  // Round up.

  size_t to_page = to_first_page;
  for (; to_page < to_last_page; to_page += PAGE_SIZE) {
    size_t physical_page_address =
        GetOrCreateVirtualPage(address_space, to_page);
    if (physical_page_address == OUT_OF_MEMORY) {
      // We ran out of memory trying to allocate the virtual page.
      return false;
    }

    size_t temp_addr =
        (size_t)TemporarilyMapPhysicalMemory(physical_page_address, 5);

    // Indices where to start/finish copying within the page.
    size_t offset_in_page_to_start_copying_at =
        to_start > to_page ? to_start - to_page : 0;
    size_t offset_in_page_to_finish_copying_at =
        to_page + PAGE_SIZE > to_end ? to_end - to_page : PAGE_SIZE;
    size_t copy_length = offset_in_page_to_finish_copying_at -
                         offset_in_page_to_start_copying_at;

    memcpy((char*)(temp_addr + offset_in_page_to_start_copying_at),
           (char*)(from_start), copy_length);

    from_start += copy_length;
  }

  return true;
}

// Touches memory, to make sure it is available, but doesn't copy anything into
// it.
bool LoadMemory(size_t to_start, size_t to_end, Process* process) {
  VirtualAddressSpace* address_space = &process->virtual_address_space;

  size_t to_first_page = to_start & ~(PAGE_SIZE - 1);  // Round down.
  size_t to_last_page =
      (to_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  // Round up.

  size_t to_page = to_first_page;
  for (; to_page < to_last_page; to_page += PAGE_SIZE) {
    size_t physical_page_address =
        GetOrCreateVirtualPage(address_space, to_page);
    if (physical_page_address == OUT_OF_MEMORY) {
      // We ran out of memory trying to allocate the virtual page.
      return false;
    }
    size_t temp_addr =
        (size_t)TemporarilyMapPhysicalMemory(physical_page_address, 5);
    // Indices where to start/finish clearing within the page.
    size_t offset_in_page_to_start_copying_at =
        to_start > to_page ? to_start - to_page : 0;
    size_t offset_in_page_to_finish_copying_at =
        to_page + PAGE_SIZE > to_end ? to_end - to_page : PAGE_SIZE;
    size_t copy_length = offset_in_page_to_finish_copying_at -
                         offset_in_page_to_start_copying_at;
    memset((char*)(temp_addr + offset_in_page_to_start_copying_at), 0,
           copy_length);
  }
  return true;
}

bool LoadSegments(const Elf64_Ehdr* header, size_t memory_start,
                  size_t memory_end, Process* process) {
  Elf64_Phdr* segment_header = (Elf64_Phdr*)(memory_start + header->e_phoff);

  // Figure out the number of segments in the binary.
  size_t number_of_segments = 0;
  if (header->e_phnum == PN_XNUM) {
    // The number of program headers is too large to fit into e_phnum. Instead,
    // it's found in the field sh_info of section 0.
    print << "Loading ELF file where e_phnum == PN_XNUM\n";
    Elf64_Shdr* section_header = (Elf64_Shdr*)(memory_start + header->e_shnum);
    if ((size_t)section_header + sizeof(Elf64_Shdr) > memory_end) {
      print << "ELF not big enough for section.\n";
      return false;
    }

    number_of_segments = section_header->sh_info;
  } else {
    number_of_segments = header->e_phnum;
  }

  // Load the segments.
  for (int i = 0; i < number_of_segments; i++, segment_header++) {
    if ((size_t)segment_header + sizeof(Elf64_Phdr) > memory_end) {
      print << "ELF not big enough for segment.\n";
      return false;
    }

    if (segment_header->p_type != PT_LOAD) {
      // Skip segments that aren't to be loaded into memory.
      continue;
    }

    if (segment_header->p_vaddr + segment_header->p_memsz >
        VIRTUAL_MEMORY_OFFSET) {
      print << "Trying to load data into kernel memory.\n";
      return false;
    }

    if (segment_header->p_filesz > 0) {
      // There is data from the file we need to copy into memory.
      size_t from_start = memory_start + segment_header->p_offset;
      size_t from_size = segment_header->p_filesz;

      if (from_start + from_size > memory_end) {
        // Segment is out of bounds of the ELF file.
        print
            << "Segment is trying to load memory that is out of bounds of the "
               "file.\n";
        return false;
      }

      size_t to_address = segment_header->p_vaddr;
      size_t to_end = to_address + from_size;
      // Copy the data from the file into memory.
      if (!CopyIntoMemory(from_start, to_address, to_end, process)) {
        return false;
      }
    }

    if (segment_header->p_memsz > segment_header->p_filesz) {
      // This is memory that takes up no space in the ELF file, but must
      // be initialized to 0 for the program.

      // Skip over any data that was copied.
      size_t to_start = segment_header->p_vaddr + segment_header->p_filesz;
      size_t to_end =
          to_start + (segment_header->p_memsz - segment_header->p_filesz);
      if (!LoadMemory(to_start, to_end, process)) {
        return false;
      }
    }
  }

  return true;
}

// Parses the name of the process, which has the permissions of the process
// before the title, e.g. "abc Some Program" is a process titled "Some Program"
// with the permissions a, b, and c.
bool ParseName(char** name, size_t* name_length, bool* is_driver,
               bool* can_create_processes) {
  *name_length = strlen(*name);

  while (true) {
    if (*name_length == 0) return false;  // Out of letters.

    if (**name == ' ') {
      // Reached a space.

      // Jumped over the space.
      (*name_length)--;
      (*name)++;

      // A valid name if we still have at least 1 character.
      return *name_length >= 1;
    }

    // Switch over this permission.
    switch (**name) {
      case 'd':
        *is_driver = true;
        break;
      case 'l':
        *can_create_processes = true;
        break;
      case '-':
        break;
      default:
        print << "Unknown attribute '" << **name << "'.";
        return false;
    }

    // Jump over this character.
    (*name)++;
    (*name_length)--;
  }
}

}  // namespace

void LoadElfProcess(size_t memory_start, size_t memory_end, char* name) {
  char* original_name = name;
  size_t name_length = 0;
  bool is_driver = false;
  bool can_create_processes = false;

  if (!ParseName(&name, &name_length, &is_driver, &can_create_processes)) {
    print << "Can't load module \"" << original_name
          << "\" because the name is not in the correct format.\n";
    return;
  }

  if (is_driver) {
    print << "Loading driver ";
  } else {
    print << "Loading application ";
  }

  print << name << "...\n";

  if (memory_start + sizeof(Elf64_Ehdr) > memory_end) {
    print << "ELF not big enough for header.\n";
    return;
  }

  Elf64_Ehdr* header = (Elf64_Ehdr*)memory_start;
  if (!IsValidElfHeader(header)) {
    return;
  }

  Process* process = CreateProcess(is_driver, can_create_processes);
  if (!process) {
    print << "Out of memory to create the process.\n";
    return;
  }

  CopyString(name, PROCESS_NAME_LENGTH, name_length, (char*)process->name);

  if (!LoadSegments(header, memory_start, memory_end, process)) {
    print << "Destroying process.\n";
    DestroyProcess(process);
    return;
  }

  Thread* thread = CreateThread(process, header->e_entry, 0);
  if (!thread) {
    print << "Out of memory to create the thread.\n";
    DestroyProcess(process);
    return;
  }

  ScheduleThread(thread);
}
