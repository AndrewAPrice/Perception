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

#include "perception/memory.h"

#include <iostream>

#include "perception/debug.h"

#if !defined(PERCEPTION) || defined(TEST)
#include <stdlib.h>
#include <unistd.h>
#endif

namespace perception {
namespace {

constexpr size_t kOutOfMemory = 1;

}  // namespace

void* AllocateMemoryPages(size_t number) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall_num __asm__("rdi") = 12;
  volatile register size_t param1 __asm__("rax") = number;
  volatile register size_t param2 __asm__("rdx") = 0;
  volatile register size_t return_val __asm__("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val)
                       : "r"(syscall_num), "r"(param1), "r"(param2)
                       : "rcx", "r11");
  if (return_val == kOutOfMemory) {
    DebugPrinterSingleton << "AllocateMemoryPages returned out of memory\n";
    return nullptr;
  } else if (return_val == 0) {
    DebugPrinterSingleton << "AllocateMemoryPages returned 0\n";
    return nullptr;
  } else
    return (void*)return_val;
#else
  return malloc(kPageSize * number);
#endif
}

void* AllocateMemoryPagesBelowPhysicalAddressBase(
    size_t number, size_t max_base_address, size_t& first_physical_address) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall_num asm("rdi") = 49;
  volatile register size_t param1 asm("rax") = number;
  volatile register size_t param2 asm("rbx") = max_base_address;
  volatile register size_t return_val asm("rax");
  volatile register size_t first_physical_address_r asm("rbx");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val), "=r"(first_physical_address_r)
                       : "r"(syscall_num), "r"(param1), "r"(param2)
                       : "rcx", "r11");
  if (return_val == kOutOfMemory) {
    first_physical_address = 0;
    return nullptr;
  } else {
    first_physical_address = first_physical_address_r;
    return (void*)return_val;
  }
#else
  first_physical_address = 0;
  return nullptr;
#endif
}

void ReleaseMemoryPages(void* ptr, size_t number) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall_num asm("rdi") = 13;
  volatile register size_t param1 asm("rax") = (size_t)ptr;
  volatile register size_t param2 asm("rbx") = number;

  __asm__ __volatile__("syscall\n" ::"r"(syscall_num), "r"(param1), "r"(param2)
                       : "rcx", "r11");
#else
  return free(ptr);
#endif
}

// Maps physical memory into this process's address space. Only drivers
// may call this.
void* MapPhysicalMemory(size_t physical_address, size_t pages) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall_num asm("rdi") = 41;
  volatile register size_t physical_address_r asm("rax") = physical_address;
  volatile register size_t pages_r asm("rbx") = pages;
  volatile register size_t return_val asm("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val)
                       : "r"(syscall_num), "r"(physical_address_r), "r"(pages_r)
                       : "rcx", "r11");
  if (return_val == kOutOfMemory)
    return nullptr;
  else
    return (void*)return_val;
#else
  return nullptr;
#endif
}

size_t GetPhysicalAddressOfVirtualAddress(size_t virtual_address) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall_num asm("rdi") = 50;
  volatile register size_t param1 asm("rax") = virtual_address;
  volatile register size_t return_val asm("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val)
                       : "r"(syscall_num), "r"(param1)
                       : "rcx", "r11");
  if (return_val == kOutOfMemory) {
    return 0;
  } else {
    size_t offset_in_page = virtual_address & (kPageSize - 1);
    return return_val + offset_in_page;
  }
#else
  return virtual_address;
#endif
}

bool MaybeResizePages(void** ptr, size_t current_number, size_t new_number) {
#if defined(PERCEPTION) && !defined(TEST)
  std::cout << "Implement MaybeResizePages." << std::endl;
  return false;
#else
  void* maybe_new_ptr = realloc(*ptr, new_number * kPageSize);
  if (maybe_new_ptr == nullptr) return false;

  *ptr = maybe_new_ptr;
  return true;
#endif
}

void GetSystemMemoryMetrics(size_t& total_memory, size_t& shared_memory,
                            size_t& free_memory) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t rdi_io asm("rdi") = 14;
  volatile register size_t total_r asm("rax");
  volatile register size_t shared_r asm("rbx");
  volatile register size_t free_r asm("rdx");

  __asm__ __volatile__("syscall\n"
                       : "=r"(total_r), "=r"(shared_r), "=r"(free_r)
                       : "r"(rdi_io)
                       : "rcx", "r11");
  total_memory = total_r;
  shared_memory = shared_r;
  free_memory = free_r;
#else
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  total_memory = pages * page_size;
  shared_memory = 0;
  free_memory = total_memory / 2;
#endif
}

size_t GetFreeSystemMemory() {
  size_t total, shared, free;
  GetSystemMemoryMetrics(total, shared, free);
  return free;
}

size_t GetTotalSystemMemory() {
  size_t total, shared, free;
  GetSystemMemoryMetrics(total, shared, free);
  return total;
}

void GetProcessHealthMetrics(ProcessId pid, size_t& unique_memory_used,
                             size_t& shared_memory_used,
                             size_t& creation_timestamp,
                             size_t& registered_services,
                             uint8* cpu_percentages) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t rax_io asm("rax") = pid;
  volatile register size_t rdi_io asm("rdi") = 15;
  volatile register size_t creation_timestamp_r asm("rbx");
  volatile register size_t packed_cpu_r asm("rdx");
  volatile register size_t registered_services_r asm("rsi");

  __asm__ __volatile__("syscall\n"
                       : "+r"(rax_io), "+r"(rdi_io), "=r"(creation_timestamp_r),
                         "=r"(packed_cpu_r), "=r"(registered_services_r)
                       :
                       : "rcx", "r11");

  unique_memory_used = rax_io;
  shared_memory_used = rdi_io;
  creation_timestamp = creation_timestamp_r;
  registered_services = registered_services_r;

  if (cpu_percentages != nullptr) {
    size_t packed = packed_cpu_r;
    for (int i = 0; i < 8; i++) {
      cpu_percentages[i] = (uint8)((packed >> (i * 8)) & 0xFF);
    }
  }
#else
  unique_memory_used = 0;
  shared_memory_used = 0;
  creation_timestamp = 0;
  registered_services = 0;
  if (cpu_percentages != nullptr) {
    for (int i = 0; i < 8; i++) {
      cpu_percentages[i] = 0;
    }
  }
#endif
}

void SetThatProcessCaresAboutCpuTracking(bool active) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall_num asm("rdi") = 64;
  volatile register size_t param_r asm("rax") = active ? 1 : 0;

  __asm__ __volatile__("syscall\n"
                       :
                       : "r"(syscall_num), "r"(param_r)
                       : "rcx", "r11");
#endif
}

void SetMemoryAccessRights(void* address, size_t pages, bool can_write,
                           bool can_execute) {
#if defined(PERCEPTION) && !defined(TEST)
  size_t rights = 0;
  if (can_write) rights |= 1;
  if (can_execute) rights |= 2;

  volatile register size_t syscall_num asm("rdi") = 48;
  volatile register size_t physical_address_r asm("rax") = (size_t)address;
  volatile register size_t pages_r asm("rbx") = pages;
  volatile register size_t rights_r asm("rdx") = rights;

  __asm__ __volatile__("syscall\n"
                       :
                       : "r"(syscall_num), "r"(physical_address_r),
                         "r"(pages_r), "r"(rights_r)
                       : "rcx", "r11");
#endif
}

}  // namespace perception

#if defined(PERCEPTION) && !defined(TEST)
// Function that runs if a virtual function implementation is missing. Should
// never be called but needs to exist. extern "C" void __cxa_pure_virtual() {}

#include <stddef.h>

extern "C" {
void* malloc(size_t);
void free(void*);
}

// Functions to support new/delete.
void* operator new(long unsigned int size) {
  void* p = malloc(size);
  if (p == nullptr) {
    perception::DebugPrinterSingleton << "malloc() returned nullptr\n";
  }
  return p;
}

void* operator new[](long unsigned int size) {
  void* p = malloc(size);
  if (p == nullptr) {
    perception::DebugPrinterSingleton << "malloc() returned nullptr\n";
  }
  return p;
}

void operator delete(void* address) noexcept { free(address); }

void operator delete[](void* address) noexcept { free(address); }

void operator delete(void* address, long unsigned int size) noexcept { free(address); }

void operator delete[](void* address, long unsigned int size) noexcept { free(address); }
#endif
