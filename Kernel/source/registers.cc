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

#include "registers.h"

#include "text_terminal.h"

// Prints the registers, for debugging.
void PrintRegisters(struct Registers* regs) {
  print << "Printing registers:\n" << NumberFormat::Hexidecimal <<
    "rax: " << regs->rax << "   rbx: " << regs->rbx <<
    "\nrcx: " << regs->rcx << "   rdx: " << regs->rdx <<
    "\nrsp: " << regs->rsp << "   rbp: " << regs->rbp <<
    "\nrsi: " << regs->rsi << "   rdi: " << regs->rdi <<
    "\n r8: " << regs->r8 << "    r9: " << regs->r9 <<
    "\nr10: " << regs->r10 << "   r11: " <<regs->r11 <<
    "\nr12: " << regs->r12 << "   r13: " << regs->r13 <<
    "\nr14: " << regs->r14 << "   r15: " << regs->r15 <<
    "\n cs: " << regs->cs << "    ss: " << regs->ss <<
    "\nrip: " << regs->rip << " flags: " << regs->rflags << '\n';
}
