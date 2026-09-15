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

#include "perception/acpi.h"

namespace perception {

namespace {

// Syscall number for querying ACPI details.
constexpr size_t kGetAcpiDetailsSyscall = 73;

}  // namespace

bool GetAcpiDetails(AcpiDetails& details) {
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = kGetAcpiDetailsSyscall;
  volatile register size_t rax asm("rax");
  volatile register size_t rbx asm("rbx");
  volatile register size_t rdx asm("rdx");
  volatile register size_t rsi asm("rsi");
  volatile register size_t r8 asm("r8");

  __asm__ __volatile__("syscall\n"
                       : "=r"(rax), "=r"(rbx), "=r"(rdx), "=r"(rsi), "=r"(r8)
                       : "r"(syscall)
                       : "rcx", "r11");

  details.pm1a_control_port = static_cast<uint16>(rax & 0xFFFF);
  details.pm1b_control_port = static_cast<uint16>((rax >> 16) & 0xFFFF);

  details.slp_typa = static_cast<uint8>(rbx & 0xFF);
  details.slp_typb = static_cast<uint8>((rbx >> 8) & 0xFF);
  details.has_s5 = ((rbx >> 16) & 1) != 0;

  details.smi_cmd_port = static_cast<uint16>(rdx & 0xFFFF);
  details.acpi_enable_value = static_cast<uint8>((rdx >> 16) & 0xFF);

  details.reset_port = static_cast<uint16>(rsi & 0xFFFF);
  details.reset_value = static_cast<uint8>((rsi >> 16) & 0xFF);
  details.has_reset = ((rsi >> 24) & 1) != 0;

  details.rsdp_physical_address = r8;

  return details.has_s5 || details.has_reset || (details.rsdp_physical_address != 0);
#else
  return false;
#endif
}

}  // namespace perception
