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
constexpr int kMaximumModuleNameLength = 10 * 8;

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
  volatile register size_t address_and_flags_r asm("rdi");
  volatile register size_t size_r asm("r15");
  volatile register size_t name_word0 asm("rax");
  volatile register size_t name_word1 asm("rbx");
  volatile register size_t name_word2 asm("rdx");
  volatile register size_t name_word3 asm("rsi");
  volatile register size_t name_word4 asm("r8");
  volatile register size_t name_word5 asm("r9");
  volatile register size_t name_word6 asm("r10");
  volatile register size_t name_word7 asm("r12");
  volatile register size_t name_word8 asm("r13");
  volatile register size_t name_word9 asm("r14");

  __asm__ __volatile__("syscall\n"
                       : "=r"(address_and_flags_r), "=r"(size_r),
                         "=r"(name_word0), "=r"(name_word1), "=r"(name_word2),
                         "=r"(name_word3), "=r"(name_word4), "=r"(name_word5),
                         "=r"(name_word6), "=r"(name_word7), "=r"(name_word8),
                         "=r"(name_word9)
                       : "r"(syscall)
                       : "rcx", "r11", "memory");

  size_t size = size_r;
  if (size == 0) return nullptr;

  size_t address_and_flags = address_and_flags_r;
  size_t name_words[10];
  name_words[0] = name_word0;
  name_words[1] = name_word1;
  name_words[2] = name_word2;
  name_words[3] = name_word3;
  name_words[4] = name_word4;
  name_words[5] = name_word5;
  name_words[6] = name_word6;
  name_words[7] = name_word7;
  name_words[8] = name_word8;
  name_words[9] = name_word9;

  auto multiboot_module = std::make_unique<MultibootModule>();
  multiboot_module->flags = address_and_flags & (4096 - 1);
  multiboot_module->data =
      MemorySpan((void*)(address_and_flags ^ multiboot_module->flags), size);

  // Add an extra byte for the null terminator.
  char module_name[kMaximumModuleNameLength + 1];

  // Copy the string out of the registers into a char array.
  for (int i = 0; i < 10; ++i) ((size_t*)module_name)[i] = name_words[i];

  module_name[kMaximumModuleNameLength] = '\0';
  multiboot_module->name = std::string(module_name);

  return multiboot_module;
#else   // PERCEPTION
  return nullptr;
#endif  // PERCEPTIOn
}

}  // namespace perception
