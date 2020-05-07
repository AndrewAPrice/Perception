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

#include "../../third_party/elf.h"
#include "io.h"
#include "physical_allocator.h"
#include "process.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_allocator.h"

// Is this a valid ELF header?
bool IsValidElfHeader(Elf64_Ehdr* header) {
	if (header->e_ident[EI_MAG0] != ELFMAG0 ||
		header->e_ident[EI_MAG1] != ELFMAG1 ||
		header->e_ident[EI_MAG2] != ELFMAG2 ||
		header->e_ident[EI_MAG3] != ELFMAG3) {
		PrintString("Invalid ELF header.");
		return false;
	}

	if (header->e_ident[EI_CLASS] != ELFCLASS64) {
		PrintString("Not a 64-bit ELF header.");
		return false;
	}

	if (header->e_ident[EI_DATA] != ELFDATA2LSB) {
		PrintString("Not little endian.");
		return false;
	}

	if (header->e_ident[EI_VERSION] != EV_CURRENT) {
		PrintString("Not an ELF header version.");
		return false;
	}

	if (header->e_type != ET_EXEC) {
		PrintString("Not an executable file.");
		return false;
	}

	if (header->e_machine != EM_X86_64) {
		PrintString("Not an X86_64 binary.");
		return false;
	}

	return true;
}

// Copies data from the module into the process's memory.
bool CopyIntoMemory(size_t from_start,
	size_t to_start, size_t to_end, struct Process* process) {
	/*
	PrintString("Copy section ");
	PrintHex(from_start);
	PrintString(" to ");
	PrintHex(to_start);
	PrintString("->");
	PrintHex(to_end);
	PrintChar('\n');
	*/

	size_t pml4 = process->pml4;

	// The process's memory is mapped into pages. We'll copy page by page.
	size_t from_first_page = from_start & ~(PAGE_SIZE - 1); // Round down.
	size_t to_first_page = to_start & ~(PAGE_SIZE - 1); // Round down.
	size_t to_last_page = (to_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); // Round up.
	
	size_t to_page = to_first_page;
	for (; to_page < to_last_page; from_start += PAGE_SIZE, to_page += PAGE_SIZE) {
		size_t physical_page_address = GetOrCreateVirtualPage(pml4, to_page);
		if (physical_page_address == OUT_OF_MEMORY) {
			// We ran out of memory trying to allocate the virtual page.
			return false;
		}

		size_t temp_addr = (size_t)TemporarilyMapPhysicalMemory(physical_page_address, 5);

		// Indices where to start/finish copying within the page.
		size_t offset_in_page_to_start_copying_at =	to_start > to_page ? to_start - to_page : 0;
		size_t offset_in_page_to_finish_copying_at = to_page + PAGE_SIZE > to_end ? to_end - to_page : PAGE_SIZE;

		/*
		PrintString("Copying from ");
		PrintHex(from_start);
		PrintString(" to page ");
		PrintHex(to_page + offset_in_page_to_start_copying_at);
		PrintString("->");
		PrintHex(to_page + offset_in_page_to_finish_copying_at);
		PrintChar('\n');
		*/

		memcpy((unsigned char *)(temp_addr + offset_in_page_to_start_copying_at),
			(unsigned char*)(from_start),
			offset_in_page_to_finish_copying_at - offset_in_page_to_start_copying_at);
	}

	return true;

}

bool LoadSections(const Elf64_Ehdr* header,
			size_t memory_start, size_t memory_end,
			struct Process* process) {
	Elf64_Shdr* section_header = (Elf64_Shdr*)(memory_start + header->e_shoff);

	for (int i = 0; i < header->e_shnum; i++, section_header++) {
		if ((size_t)section_header + sizeof(Elf64_Shdr) > memory_end) {
			PrintString("ELF not big enough for section.");
			return false;
		}

		if (section_header->sh_type == SHT_NULL) {
			// There are many different section types. We won't worry
			// about the type specifically, but just try to load it into
			// memory. But, SHT_NULL is specifically one we should skip.
			continue;
		}

		if (!(section_header->sh_flags & SHF_ALLOC)) {
			// Section doesn't occupy memory during runtime.
			continue;
		}

		size_t section_start = memory_start + section_header->sh_offset;
		size_t section_size = section_header->sh_size;

		if (section_size == 0) {
			// Empty section, nothing to load.
			continue;
		}

		size_t virtual_address_start = section_header->sh_addr;
		size_t virtual_address_end = virtual_address_start + section_size;
		if (virtual_address_end > VIRTUAL_MEMORY_OFFSET) {
			PrintString("Trying to load data into kernel memory.");
			return false;
		}

		if (!CopyIntoMemory(section_start,
			virtual_address_start, virtual_address_end,
			process)) {
			return false;
		}

	}

	return true;

}

void LoadElfProcess(size_t memory_start, size_t memory_end, char* name) {
	PrintString("Loading module ");
	PrintString(name);
	PrintString("...\n");

	if (memory_start + sizeof(Elf64_Ehdr) > memory_end) {
		PrintString("ELF not big enough for header.");
		return;
	}

	Elf64_Ehdr* header = (Elf64_Ehdr*)memory_start;
	if (!IsValidElfHeader(header)) {
		return;
	}

	struct Process* process = CreateProcess();
	if (!process) {
		PrintString("Out of memory to create the process.");
		return;
	}

	CopyString(name, 256, strlen(name), process->name);

	if (!LoadSections(header, memory_start, memory_end, process)) {
		DestroyProcess(process);
		return;
	}

	struct Thread* thread = CreateThread(process, header->e_entry, 0);

	if (!thread) {
		PrintString("Out of to create the thread.");
		DestroyProcess(process);
		return;
	}

	ScheduleThread(thread);
}