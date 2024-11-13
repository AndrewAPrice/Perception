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

namespace perception {

// Maximum module name length.
constexpr int kMaximumModuleNameLength = 88;

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
  volatile register size_t address_and_flags asm("rdi");
#ifndef optimized_BUILD_
  volatile register size_t size asm("r11");
#else
  volatile register size_t size asm("rbp");
#endif
  volatile register size_t name_1 asm("rax");
  volatile register size_t name_2 asm("rbx");
  volatile register size_t name_3 asm("rdx");
  volatile register size_t name_4 asm("rsi");
  volatile register size_t name_5 asm("r8");
  volatile register size_t name_6 asm("r9");
  volatile register size_t name_7 asm("r10");
  volatile register size_t name_8 asm("r12");
  volatile register size_t name_9 asm("r13");
  volatile register size_t name_10 asm("r14");
  volatile register size_t name_11 asm("r15");

  __asm__ __volatile__(
#ifndef optimized_BUILD_
      R"(
    push %%rbp
    syscall
    mov %%rbp, %%r11
    pop %%rbp
  )"
#else
      "syscall\n"
#endif
      : "=r"(address_and_flags), "=r"(size), "=r"(name_1), "=r"(name_2),
        "=r"(name_3), "=r"(name_4), "=r"(name_5), "=r"(name_6), "=r"(name_7),
        "=r"(name_8), "=r"(name_9), "=r"(name_10), "=r"(name_11)
      : "r"(syscall)
      : "rcx"
#ifdef optimized_BUILD_
        ,
        "r11"
#endif
  );

  if (size == 0) return nullptr;

  auto multiboot_module = std::make_unique<MultibootModule>();
  multiboot_module->flags = address_and_flags & (4096 - 1);
  multiboot_module->data = (void*)(address_and_flags ^ multiboot_module->flags);
  multiboot_module->size = size;

  // Add an extra byte for the null terminator.
  char module_name[kMaximumModuleNameLength + 1];
  module_name[kMaximumModuleNameLength] = '\0';

  // Copy the string out of the registers into a char array.
  ((size_t*)module_name)[0] = name_1;
  ((size_t*)module_name)[1] = name_2;
  ((size_t*)module_name)[2] = name_3;
  ((size_t*)module_name)[3] = name_4;
  ((size_t*)module_name)[4] = name_5;
  ((size_t*)module_name)[5] = name_6;
  ((size_t*)module_name)[6] = name_7;
  ((size_t*)module_name)[7] = name_8;
  ((size_t*)module_name)[8] = name_9;
  ((size_t*)module_name)[9] = name_10;
  ((size_t*)module_name)[10] = name_11;

  multiboot_module->name = std::string(module_name);

  return multiboot_module;
#else   // PERCEPTION
  return nullptr;
#endif  // PERCEPTIOn
}

}  // namespace perception