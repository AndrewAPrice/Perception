// Copyright 2021 Google LLC
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

#include "perception/multiboot.h"

#include "perception/memory_span.h"

namespace perception {

// Maximum module name length.
constexpr int kMaximumModuleNameLength = 80;

// Gets the details of the framebuffer set up by the bootloader.
void GetMultibootFramebufferDetails(size_t& physical_address, uint32& width,
                                    uint32& height, uint32& pitch, uint8& bpp) {
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 40;
  volatile register size_t physical_address_r asm("rax");
  volatile register size_t width_r asm("rbx");
  volatile register size_t height_r asm("rdx");
  volatile register size_t pitch_r asm("rsi");
  volatile register size_t bpp_r asm("r8");

  __asm__ __volatile__("syscall\n"
                       : "=r"(physical_address_r), "=r"(width_r),
                         "=r"(height_r), "=r"(pitch_r), "=r"(bpp_r)
                       : "r"(syscall)
                       : "rcx", "r11");

  physical_address = physical_address_r;
  width = (size_t)width_r;
  height = (size_t)height_r;
  pitch = (size_t)pitch_r;
  bpp = (size_t)bpp_r;
#else
  physical_address = 0;
  width = 0;
  height = 0;
  pitch = 0;
  bpp = 0;
#endif
}

std::unique_ptr<MultibootModule> GetMultibootModule() {
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 60;
  size_t address_and_flags;
  size_t size;
  size_t name_words[11];
  __asm__ __volatile__(
#ifndef optimized_BUILD_
      R"(
    push %%rbp
    syscall
    mov %%rbp, %1
    mov %%r8, %6
    mov %%r9, %7
    mov %%r10, %8
    mov %%r12, %9
    mov %%r13, %10
    mov %%r14, %11
    mov %%r15, %12
    pop %%rbp
  )"
#else
      R"(
    syscall
    mov %%r8, %6
    mov %%r9, %7
    mov %%r10, %8
    mov %%r12, %9
    mov %%r13, %10
    mov %%r14, %11
    mov %%r15, %12
  )"
#endif
      : "=D"(address_and_flags), "=r"(size),
        "=a"(name_words[0]), "=b"(name_words[1]), "=d"(name_words[2]),
        "=S"(name_words[3]), "=r"(name_words[4]), "=r"(name_words[5]),
        "=r"(name_words[6]), "=r"(name_words[7]), "=r"(name_words[8]),
        "=r"(name_words[9]), "=r"(name_words[10])
      : "D"(syscall)
      : "rcx"
  );

  if (size == 0) return nullptr;

  auto multiboot_module = std::make_unique<MultibootModule>();
  multiboot_module->flags = address_and_flags & (4096 - 1);
  multiboot_module->data =
      MemorySpan((void*)(address_and_flags ^ multiboot_module->flags), size);

  // Add an extra byte for the null terminator.
  char module_name[kMaximumModuleNameLength + 1];
  module_name[kMaximumModuleNameLength] = '\0';

  // Copy the string out of the registers into a char array.
  ((size_t*)module_name)[0] = name_words[0];
  ((size_t*)module_name)[1] = name_words[1];
  ((size_t*)module_name)[2] = name_words[2];
  ((size_t*)module_name)[3] = name_words[3];
  ((size_t*)module_name)[4] = name_words[4];
  ((size_t*)module_name)[5] = name_words[5];
  ((size_t*)module_name)[6] = name_words[6];
  ((size_t*)module_name)[7] = name_words[7];
  ((size_t*)module_name)[8] = name_words[8];
  ((size_t*)module_name)[9] = name_words[9];
  ((size_t*)module_name)[10] = name_words[10];

  multiboot_module->name = std::string(module_name);

  return multiboot_module;
#else   // PERCEPTION
  return nullptr;
#endif  // PERCEPTIOn
}

}  // namespace perception
