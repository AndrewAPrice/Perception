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

#include "core_dump.h"

#include "core_dump_structs.h"
#include "io.h"
#include "physical_allocator.h"
#include "process.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_allocator.h"

// #define SUPPORTS_CORE_DUMPING

#ifdef SUPPORTS_CORE_DUMPING
namespace {

// The non-printable escape code that the monitor picks up that something
// special is about to happen.
constexpr char kMonitorEscapeCode = '\xFF';

// The sequence sent to the monitor to say a core dump is happening.
constexpr char kCoreDumpMonitorSequence[] = "CoreDump";

// The name of the PT_NOTE notes.
constexpr char kCoreDumpNoteName[] = "CORE";
constexpr int kCoreDumpNoteNameCharacters = 5;  // Includes null terminator.
constexpr int kCoreDumpNoteNameMaxCharacters = 8;

constexpr size_t kCoreDumpHeaderNoteSize =
    sizeof(Elf64_Nhdr) + sizeof(elf_prpsinfo) + kCoreDumpNoteNameMaxCharacters;
constexpr size_t kCoreDumpHeaderSize = sizeof(Elf64_Ehdr)    // ELF header
                                       + sizeof(Elf64_Phdr)  // PT_NOTE header
                                       +
                                       kCoreDumpHeaderNoteSize;  // NT_PRPSINFO
constexpr size_t kCoreDumpSizePerThread =
    sizeof(elf_prstatus)      // NT_PRSTATUS
    + sizeof(elf_fpregset_t)  // NT_FPREGSET
    // + sizeof(XSAVE)           // NT_X86_XSTATE
    // + sizeof(siginfo_t)  // NT_SIGINFO
    + (sizeof(Elf64_Nhdr) + kCoreDumpNoteNameMaxCharacters) * 2;
constexpr size_t kCoreDumpSizePerMemoryChunk = sizeof(Elf64_Phdr);  // PT_LOAD

VirtualAddressSpace &VirtualAddressSpaceForProcess(Process *process) {
  return process->virtual_address_space;
}

size_t NumberOfThreadsInProcess(Process *process) {
  return process->thread_count;
}

template <typename TCallbackFunc>
void CountMemoryAndChunksInVirtualAddressSpaceAndRange(
    VirtualAddressSpace &virtual_address_space, size_t start_address,
    size_t end_address, TCallbackFunc &&on_each_memory_chunk) {
  size_t addr = start_address;

  auto *fmr = virtual_address_space.free_chunks_by_address
                  .SearchForItemGreaterThanOrEqualToValue(addr);

  while (true) {
    if (fmr == nullptr || fmr->start_address > end_address) {
      // Last memory chunk.
      size_t size = end_address - addr;
      if (size > 0) {
        on_each_memory_chunk(addr, end_address);
      }
      return;
    } else {
      size_t size = fmr->start_address - addr;
      if (size > 0) {
        on_each_memory_chunk(addr, fmr->start_address - 1);
      }
      addr = fmr->start_address + fmr->pages * PAGE_SIZE;
      fmr = virtual_address_space.free_chunks_by_address
                .SearchForItemGreaterThanOrEqualToValue(addr);
    }
  }
}

template <typename TCallbackFunc>
void OnEachMemoryChunkInVirtualAddressSpace(
    VirtualAddressSpace &virtual_address_space,
    TCallbackFunc &&on_each_memory_chunk) {
  // User space memory has a 'hole' of non-canonical addresses in the middle.
  size_t hole_start, hole_end;
  GetUserspaceVirtualMemoryHole(hole_start, hole_end, true);

  CountMemoryAndChunksInVirtualAddressSpaceAndRange(
      virtual_address_space, 0, hole_start - 1, on_each_memory_chunk);

  CountMemoryAndChunksInVirtualAddressSpaceAndRange(
      virtual_address_space, hole_end + 1, VIRTUAL_MEMORY_OFFSET - 1,
      on_each_memory_chunk);
}

size_t CoreDumpSize(Process *process, size_t threads, size_t memory_chunks,
                    size_t memory_size) {
  return kCoreDumpHeaderSize + kCoreDumpSizePerThread * threads +
         kCoreDumpSizePerMemoryChunk * memory_chunks + memory_size;
}

template <class T>
void PrintDataStructure(T &structure) {
  char *bytes = (char *)&structure;
  for (size_t i = 0; i < sizeof(T); i++) print << bytes[i];
}

size_t NumberOfProgramHeaders(size_t memory_chunks) {
  // PT_NOTE and 1 for each memory chunk.
  return 1 + memory_chunks;
}

void PrintElfHeader(size_t threads, size_t memory_chunks, size_t memory_size) {
  Elf64_Ehdr header;
  Clear(header);
  header.e_ident[EI_MAG0] = ELFMAG0;
  header.e_ident[EI_MAG1] = ELFMAG1;
  header.e_ident[EI_MAG2] = ELFMAG2;
  header.e_ident[EI_MAG3] = ELFMAG3;
  header.e_ident[EI_CLASS] = ELFCLASS64;
  header.e_ident[EI_DATA] = ELFDATA2LSB;
  header.e_ident[EI_VERSION] = EV_CURRENT;
  // It's not that Perception is GNU, but it conforms to the format that
  // debuggers can handle.
  header.e_ident[EI_OSABI] = ELFOSABI_GNU;
  header.e_type = ET_CORE;
  header.e_machine = EM_X86_64;
  header.e_version = EV_CURRENT;
  header.e_phoff = sizeof(Elf64_Ehdr);
  header.e_ehsize = sizeof(Elf64_Ehdr);
  header.e_phentsize = sizeof(Elf64_Phdr);
  // PT_NOTE and 1 for each memory chunk.
  header.e_phnum = NumberOfProgramHeaders(memory_chunks);
  PrintDataStructure(header);
}

void PrintProgramHeaders(VirtualAddressSpace *virtual_address_space,
                         size_t threads, size_t memory_chunks,
                         size_t memory_size) {
  size_t pt_notes_size =
      kCoreDumpHeaderNoteSize + (kCoreDumpSizePerThread * threads);

  // Data begins after the program headers.
  size_t offset = sizeof(Elf64_Ehdr) +
                  (NumberOfProgramHeaders(memory_chunks) * sizeof(Elf64_Phdr));

  // Program header for PT notes.
  Elf64_Phdr header;
  Clear(header);
  header.p_type = PT_NOTE;
  header.p_offset = offset;
  header.p_filesz = pt_notes_size;
  PrintDataStructure(header);

  offset += pt_notes_size;

  OnEachMemoryChunkInVirtualAddressSpace(
      virtual_address_space, [&](size_t start_address, size_t end_address) {
        size_t size = end_address - start_address + 1;
        if ((size % PAGE_SIZE) != 0) return;

        Clear(header);
        header.p_type = PT_LOAD;
        header.p_offset = offset;
        header.p_vaddr = start_address;
        header.p_memsz = size;
        header.p_filesz = size;
        header.p_flags = PF_X | PF_W | PF_R;
        PrintDataStructure(header);

        offset += size;
      });
}

void PrintElfNoteName() {
  print << kCoreDumpNoteName;
  for (int i = 0;
       i < kCoreDumpNoteNameMaxCharacters - kCoreDumpNoteNameCharacters + 1;
       i++) {
    print << '\0';
  }
}

void PrintElfNoteHeader(size_t size, Elf64_Word type) {
  Elf64_Nhdr header;
  Clear(header);
  header.n_descsz = size;
  header.n_type = type;
  header.n_namesz = kCoreDumpNoteNameCharacters;
  PrintDataStructure(header);
  PrintElfNoteName();
}

void PrintPRPSInfo(Process *process) {
  PrintElfNoteHeader(sizeof(elf_prpsinfo), NT_PRPSINFO);

  elf_prpsinfo prpsinfo;
  Clear(prpsinfo);
  // Alive = 0, Stopped = 3, Dead = 4.
  prpsinfo.pr_state = 4;
  // "RSDTZW"[pr_state] or '.'
  prpsinfo.pr_sname = 'Z';
  prpsinfo.pr_zomb = 1;
  if (process != nullptr) {
    prpsinfo.pr_pid = (unsigned int)process->pid;
    memcpy(prpsinfo.pr_fname, process->name, sizeof(elf_prpsinfo::pr_fname));
  }
  PrintDataStructure(prpsinfo);
}

void CopyRegisters(const Registers &registers, user_regs_struct &elf_regs) {
  elf_regs.r15 = registers.r15;
  elf_regs.r14 = registers.r14;
  elf_regs.r13 = registers.r13;
  elf_regs.r12 = registers.r12;
  elf_regs.rbp = registers.rbp;
  elf_regs.rbx = registers.rbx;
  elf_regs.r11 = registers.r11;
  elf_regs.r10 = registers.r10;
  elf_regs.r9 = registers.r9;
  elf_regs.r8 = registers.r8;
  elf_regs.rax = registers.rax;
  elf_regs.rcx = registers.rcx;
  elf_regs.rdx = registers.rdx;
  elf_regs.rsi = registers.rsi;
  elf_regs.rdi = registers.rdi;
  elf_regs.orig_rax = registers.rax;
  elf_regs.rip = registers.rip;
  elf_regs.cs = registers.cs;
  elf_regs.rsp = registers.rsp;
  elf_regs.ss = registers.ss;
  elf_regs.eflags = registers.rflags;
  // fs_base, gs_base
  elf_regs.ds = 0x18 | 3;
  elf_regs.es = 0x18 | 3;
  elf_regs.fs = 0x10;
  elf_regs.gs = 0x10;
}

void PrintPrStatus(Process *process, Thread *thread, int exception_no,
                   size_t cr2, size_t error_code) {
  PrintElfNoteHeader(sizeof(elf_prstatus), NT_PRSTATUS);

  elf_prstatus pr_status;
  Clear(pr_status);
  pr_status.pr_info.si_code = error_code;
  pr_status.pr_info.si_errno = exception_no;
  pr_status.pr_pid = (int)thread->id;
  pr_status.pr_fpvalid = 1;

  Registers *registers = &thread->registers;
  CopyRegisters(*registers, *(user_regs_struct *)&pr_status.pr_reg);

  PrintDataStructure(pr_status);
}

void PrintFpRegSet(Process *process, Thread *thread) {
  PrintElfNoteHeader(sizeof(elf_fpregset_t), NT_FPREGSET);

  elf_fpregset_t regset;
  Clear(regset);
  memcpy((char *)&regset, (char *)&thread->fpu_registers, 512);
  PrintDataStructure(regset);
}

/*
void PrintX86XSave(Process *process, Thread *thread) {
  PrintElfNoteHeader(sizeof(XSAVE), NT_FPREGSET);
  // This is hard-coded to be the 512-byte FXSAVE but if possible it should
  // include the entire XSAVE.
  XSAVE xsave;
  Clear(xsave);
  memcpy((char *)&xsave.i387, (char *)&thread->fpu_registers, 512);
  PrintDataStructure(xsave);
}

void PrintSigInfo(Process *process, Thread *thread, int exception_no,
                  size_t cr2, size_t error_code) {
  PrintElfNoteHeader(sizeof(siginfo_t), NT_SIGINFO);

  siginfo_t siginfo;
  Clear(siginfo);
  siginfo.si_errno = exception_no;
  siginfo.si_code = error_code;
  PrintDataStructure(siginfo);
}
*/

void PrintThreadNotes(Process *process, Thread *thread, int exception_no,
                      size_t cr2, size_t error_code) {
  PrintPrStatus(process, thread, exception_no, cr2, error_code);
  PrintFpRegSet(process, thread);
  // PrintX86XSave(process, thread);
  // PrintSigInfo(process, thread, exception_no, cr2, error_code);
}

void PrintPTNotes(Process *process, Thread *target_thread, int exception_no,
                  size_t cr2, size_t error_code) {
  PrintPRPSInfo(process);
  // Print the notes for the target thread first, followed by the other threads.
  if (target_thread != nullptr)
    PrintThreadNotes(process, target_thread, exception_no, cr2, error_code);
  if (process != nullptr) {
    for (Thread *t : process->threads) {
      if (t != target_thread) PrintThreadNotes(process, t, 0, 0, 0);
    }
  }
}

void PrintMemory(VirtualAddressSpace &virtual_address_space) {
  OnEachMemoryChunkInVirtualAddressSpace(
      virtual_address_space, [&](size_t start_address, size_t end_address) {
        size_t size = end_address - start_address + 1;
        if ((size % PAGE_SIZE) != 0) return;
        // Print each memory page.
        size_t pages = size / PAGE_SIZE;
        for (size_t addr = start_address; pages > 0;
             pages--, addr += PAGE_SIZE) {
          size_t physical_addr = GetPhysicalAddress(
              virtual_address_space, addr, /*ignore_unowned_pages=*/false);
          if (physical_addr == OUT_OF_MEMORY) {
            for (int i = 0; i < PAGE_SIZE; i++) print << '\0';
          } else {
            char *c = (char *)TemporarilyMapPhysicalPages(physical_addr, 6);
            for (int i = 0; i < PAGE_SIZE; i++) print << c[i];
          }
        }
      });
}

void PrintCoreDumpContents(VirtualAddressSpace &virtual_address_space,
                           Process *process, Thread *target_thread,
                           int exception_no, size_t cr2, size_t error_code,
                           size_t threads, size_t memory_chunks,
                           size_t memory_size) {
  PrintElfHeader(threads, memory_chunks, memory_size);
  PrintProgramHeaders(virtual_address_space, threads, memory_chunks,
                      memory_size);
  PrintPTNotes(process, target_thread, exception_no, cr2, error_code);
  PrintMemory(virtual_address_space);
}

}  // namespace

void PrintCoreDump(Process *process, Thread *target_thread, int exception_no,
                   size_t cr2, size_t error_code) {
  bool any_page_unaligned_chunks = false;
  auto &virtual_address_space = VirtualAddressSpaceForProcess(process);
  size_t threads = NumberOfThreadsInProcess(process);
  size_t memory_chunks = 0, memory_size = 0;
  OnEachMemoryChunkInVirtualAddressSpace(
      virtual_address_space, [&](size_t start_address, size_t end_address) {
        size_t size = end_address - start_address + 1;
        if ((size % PAGE_SIZE) != 0) {
          print << "Unaligned chunk of size " << NumberFormat::Decimal << size
                << " - " << NumberFormat::Hexidecimal << start_address << " -> "
                << end_address << "\n";

          any_page_unaligned_chunks = true;
          return;
        }
        print << NumberFormat::Hexidecimal << start_address << " -> "
              << end_address << "\n";
        memory_size += size;
        memory_chunks++;
      });

  if (any_page_unaligned_chunks) {
    print << "Encountered page unaligned chunks in address space. Those will "
             "be skipped.\n";
  }

  // Let the monitor know that a core dump is being output.
  print << kMonitorEscapeCode << kCoreDumpMonitorSequence << kMonitorEscapeCode
        << NumberFormat::DecimalWithoutCommas;
  // Print the length of the process name, followed by the name of the
  // process.
  print << (size_t)strlen(process->name) << kMonitorEscapeCode << process->name;
  print << CoreDumpSize(process, threads, memory_chunks, memory_size)
        << kMonitorEscapeCode;
  PrintCoreDumpContents(virtual_address_space, process, target_thread,
                        exception_no, cr2, error_code, threads, memory_chunks,
                        memory_size);
}
#else  // SUPPORTS_CORE_DUMPING

void PrintCoreDump(Process *process, Thread *target_thread, int exception_no,
                   size_t cr2, size_t error_code) {}

#endif  // SUPPORTS_CORE_DUMPING
