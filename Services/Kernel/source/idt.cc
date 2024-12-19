#include "idt.h"

#include "memory.h"
#include "physical_allocator.h"
#include "text_terminal.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

namespace {

// An entry in the interrupt descriptor table.
struct idt_entry {
  unsigned short base_low;
  unsigned short sel;
  unsigned char always0;
  unsigned char flags;
  unsigned short base_middle;
  unsigned int base_high;
  unsigned int zero; /* reserved */
} __attribute__((packed));

// Pointer to the interrupt descriptor table. This is an array of up to 256
// entries.
idt_entry *idt;

// Reference to the interrupt descriptor.
struct idt_ptr {
  unsigned short limit;
  size_t base;
} __attribute__((packed));

// A reference to the interrupt descriptor table.
idt_ptr idt_p;

unsigned char in_interrupt;

}  // namespace

// Initializes the interrupt descriptor table.
void InitializeIdt() {
  in_interrupt = false;

  // The IDT aligns with pages - so grab a page to allocate it.
  idt = (idt_entry *)KernelAddressSpace().AllocatePages(1);

  size_t idt_physical =
      KernelAddressSpace().GetPhysicalAddress((size_t)idt, false);

  // Populate the IDT reference to point to the physical memory location where
  // the IDT will be.
  idt_p.limit = (sizeof(idt_entry) * 256) - 1;
  idt_p.base = (size_t)idt;

  // Clear the IDT.
  memset((char *)idt, 0, sizeof(idt_entry) * 256);

// Load the new IDT pointer, which is in virtual address space.
#ifndef __TEST__
  __asm__ __volatile__("lidt (%0)" : : "b"((size_t)&idt_p));
#endif
}

void SetIdtEntry(unsigned char num, size_t handler_address, unsigned short sel,
                 unsigned char flags) {
  idt[num].base_low = (handler_address & 0xFFFF);
  idt[num].base_middle = (handler_address >> 16) & 0xFFFF;
  idt[num].base_high = (handler_address >> 32) & 0xFFFFFFFF;

  idt[num].sel = sel;
  idt[num].always0 = 0;
  idt[num].flags = flags;
  idt[num].zero = 0;
}

// Marks the interrupt handler as entered. This is already called for you for
// interrupt handlers registered with InstallHardwareInterruptHandler.
void MarkInterruptHandlerAsEntered() { in_interrupt = true; }

// Marks the interrupt handler as left. This is already called for you for
// interrupt handlers registered with InstallHardwareInterruptHandler.
void MarkInterruptHandlerAsLeft() { in_interrupt = false; }
