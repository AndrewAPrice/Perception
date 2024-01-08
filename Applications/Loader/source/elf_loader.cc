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

#include "elf_loader.h"

#include <iostream>
#include <map>

#include "elf.h"
#include "perception/memory.h"
#include "perception/processes.h"
#include "perception/shared_memory.h"
#include "permebuf/Libraries/perception/storage_manager.permebuf.h"

using ::perception::AllocateMemoryPages;
using ::perception::kPageSize;
using ::perception::ProcessId;
using ::perception::ReleaseMemoryPages;
using ::perception::SetChildProcessMemoryPage;
using ::perception::SharedMemory;
using ::perception::Status;
using ::permebuf::perception::MemoryMappedFile;
using ::permebuf::perception::StorageManager;

namespace {

bool IsValidElfHeader(Elf64_Ehdr* header) {
  if (header == nullptr) {
    std::cout << "ELF not big enough for header." << std::endl;
    return false;
  }

  if (header->e_ident[EI_MAG0] != ELFMAG0 ||
      header->e_ident[EI_MAG1] != ELFMAG1 ||
      header->e_ident[EI_MAG2] != ELFMAG2 ||
      header->e_ident[EI_MAG3] != ELFMAG3) {
    std::cout << "Invalid ELF header." << std::endl;
    return false;
  }

  if (header->e_ident[EI_CLASS] != ELFCLASS64) {
    std::cout << "Not a 64-bit ELF header." << std::endl;
    return false;
  }

  if (header->e_ident[EI_DATA] != ELFDATA2LSB) {
    std::cout << "Not little endian." << std::endl;
    return false;
  }

  if (header->e_ident[EI_VERSION] != EV_CURRENT) {
    std::cout << "Not an ELF header version." << std::endl;
    return false;
  }

  if (header->e_type != ET_EXEC) {
    std::cout << "Not an executable file." << std::endl;
    return false;
  }

  if (header->e_machine != EM_X86_64) {
    std::cout << "Not an X86_64 binary." << std::endl;
    return false;
  }

  return true;
}

// Returns a pointer into the child page (allocating it memory if it doesn't yet
// exists), or nullptr if it couldn't be allocated.
void* GetChildPage(size_t page_address,
                   std::map<size_t, void*>& child_memory_pages) {
  auto itr = child_memory_pages.find(page_address);
  if (itr != child_memory_pages.end())
    return itr->second;  // Already allocated.

  // Allocate this memory page.
  void* memory = AllocateMemoryPages(/*pages=*/1);
  if (memory == nullptr) return nullptr;  // Couldn't allocate memory.

  child_memory_pages.insert({page_address, memory});
  return memory;
}

// Copies data from the file into the process's memory.
bool CopyIntoMemory(void* data, size_t size, size_t address,
                    std::map<size_t, void*>& child_memory_pages) {
  size_t address_end = address + size;

  size_t first_page = address & ~(kPageSize - 1);  // Round down.
  size_t last_page =
      (address_end + kPageSize - 1) & ~(kPageSize - 1);  // Round up.

  size_t page = first_page;
  for (; page < last_page; page += kPageSize) {
    size_t memory = (size_t)GetChildPage(page, child_memory_pages);
    if (memory == (size_t) nullptr) {
      std::cout << "Couldn't allocate memory to child page." << std::endl;
      return false;
    }

    // Indices where to start/finish clearing within the page.
    size_t offset_in_page_to_start_copying_at =
        address > page ? address - page : 0;
    size_t offset_in_page_to_finish_copying_at =
        page + kPageSize > address_end ? address_end - page : kPageSize;

    size_t copy_length = offset_in_page_to_finish_copying_at -
                         offset_in_page_to_start_copying_at;

    memcpy((unsigned char*)(memory + offset_in_page_to_start_copying_at), data,
           copy_length);

    data = (void*)((size_t)data + copy_length);
  }
  return true;
}

// Touches memory, making sure it is available, but doesn't copy anything into
// it.
bool LoadMemory(size_t address, size_t size,
                std::map<size_t, void*>& child_memory_pages) {
  size_t address_end = address + size;

  size_t first_page = address & ~(kPageSize - 1);  // Round down.
  size_t last_page =
      (address_end + kPageSize - 1) & ~(kPageSize - 1);  // Round up.

  size_t page = first_page;
  for (; page < last_page; page += kPageSize) {
    size_t memory = (size_t)GetChildPage(page, child_memory_pages);
    if (memory == (size_t) nullptr) {
      std::cout << "Couldn't allocate memory to child page." << std::endl;
      return false;
    }

    // Indices where to start/finish clearing within the page.
    size_t offset_in_page_to_start_copying_at =
        address > page ? address - page : 0;
    size_t offset_in_page_to_finish_copying_at =
        page + kPageSize > address_end ? address_end - page : kPageSize;

    size_t copy_length = offset_in_page_to_finish_copying_at -
                         offset_in_page_to_start_copying_at;
    memset((unsigned char*)(memory + offset_in_page_to_start_copying_at), 0,
           copy_length);
  }
  return true;
}

// Frees the child memory pages. Used in flows where the child isn't
// successfully created.
void FreeChildMemoryPages(std::map<size_t, void*>& child_memory_pages) {
  for (std::pair<size_t, void*> addr_and_memory : child_memory_pages)
    ReleaseMemoryPages(addr_and_memory.second, /*number=*/1);
}

// Sends the memory pages to the child.
void SendMemoryPagesToChild(ProcessId child_pid,
                            std::map<size_t, void*>& child_memory_pages) {
  for (std::pair<size_t, void*> addr_and_memory : child_memory_pages) {
    SetChildProcessMemoryPage(child_pid, (size_t)addr_and_memory.second,
                              addr_and_memory.first);
  }
}

bool LoadSegments(const Elf64_Ehdr* header, SharedMemory& file_buffer,
                  ProcessId child_pid) {
  // Figure out the number of segments in the binary.
  size_t number_of_segments = 0;
  if (header->e_phnum == PN_XNUM) {
    // The number of program headers is too large to fit into e_phnum. Instead,
    // it's found in the field sh_info of section 0.
    Elf64_Shdr* section_header = (Elf64_Shdr*)(file_buffer.GetRangeAtOffset(
        header->e_shnum, sizeof(Elf64_Shdr*)));
    if (section_header == nullptr) {
      std::cout << "ELF not big enough for section." << std::endl;
      return false;
    }
    number_of_segments = section_header->sh_info;
  } else {
    number_of_segments = header->e_phnum;
  }

  // These are memory pages to assign to the new child. This must be cleaned up
  // in all return paths.
  std::map<size_t, void*> child_memory_pages;

  // Load the segments.
  for (int i = 0; i < number_of_segments; i++) {
    Elf64_Phdr* segment_header = (Elf64_Phdr*)file_buffer.GetRangeAtOffset(
        header->e_phoff + sizeof(Elf64_Phdr) * i, sizeof(Elf64_Phdr));
    if (segment_header == nullptr) {
      std::cout << "ELF not big enough for segment header." << std::endl;
      FreeChildMemoryPages(child_memory_pages);
      return false;
    }

    if (segment_header->p_type != PT_LOAD) {
      // Skip segments that aren't to be loaded into memory.
      continue;
    }

    if (segment_header->p_filesz > 0) {
      // There is data from the file we need to copy into memory.
      void* data = file_buffer.GetRangeAtOffset(segment_header->p_offset,
                                                segment_header->p_filesz);
      if (data == nullptr) {
        std::cout << "Segment is trying to load memory that is out of bounds "
                     "of the file."
                  << std::endl;
        FreeChildMemoryPages(child_memory_pages);
        return false;
      }

      size_t address = segment_header->p_vaddr;
      // Copy the data from the file into memory.
      if (!CopyIntoMemory(data, segment_header->p_filesz, address,
                          child_memory_pages)) {
        FreeChildMemoryPages(child_memory_pages);
        return false;
      }
    }

    if (segment_header->p_memsz > segment_header->p_filesz) {
      // This is memory that takes up no space in the ELF file, but must
      // be initialized to 0 for the program.

      // SKip over any data that was copied.
      size_t address = segment_header->p_vaddr + segment_header->p_filesz;
      size_t size = segment_header->p_memsz - segment_header->p_filesz;
      if (!LoadMemory(address, size, child_memory_pages)) {
        FreeChildMemoryPages(child_memory_pages);
        return false;
      }
    }
  }

  // Send the memory pages to the child.
  SendMemoryPagesToChild(child_pid, child_memory_pages);
  return true;
}

}  // namespace

StatusOr<size_t> LoadElfAndGetEntryAddress(ProcessId child_pid,
                                           std::string_view path) {
  // Open the ELf as a memory mapped file.
  Permebuf<StorageManager::OpenMemoryMappedFileRequest> request;
  request->SetPath(path);

  ASSIGN_OR_RETURN(
      auto response,
      StorageManager::Get().CallOpenMemoryMappedFile(std::move(request)));

  MemoryMappedFile file = response.GetFile();
  SharedMemory file_buffer = response.GetFileContents().Clone();

  // Check that the header is valid.
  Elf64_Ehdr* header =
      (Elf64_Ehdr*)file_buffer.GetRangeAtOffset(0, sizeof(Elf64_Ehdr));
  if (!IsValidElfHeader(header)) {
    file.SendCloseFile(MemoryMappedFile::CloseFileMessage());
    return Status::INVALID_ARGUMENT;
  }

  // Load the segments.
  bool segments_loaded = LoadSegments(header, file_buffer, child_pid);
  file.SendCloseFile(MemoryMappedFile::CloseFileMessage());
  if (segments_loaded) {
    // Return the entry point.
    return header->e_entry;
  } else {
    return Status::INVALID_ARGUMENT;
  }
}
