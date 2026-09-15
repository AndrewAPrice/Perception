#ifndef TEST
#include "interrupts/idt.h"

#include "memory/memory.h"
#include "memory/physical_allocator.h"
#include "output/text_terminal.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"

namespace interrupts {
 
using memory::KernelAddressSpace;

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
idt_entry* g_idt;

// Reference to the interrupt descriptor.
struct idt_ptr {
  unsigned short limit;
  size_t base;
} __attribute__((packed));

// A reference to the interrupt descriptor table.
idt_ptr g_idt_p;

}  // namespace

// Initializes the interrupt descriptor table.
void InitializeIdt() {
  // The IDT aligns with pages - so grab a page to allocate it.
  g_idt = (idt_entry*)KernelAddressSpace().AllocatePages(1);

  // Populate the IDT reference to point to the physical memory location where
  // the IDT will be.
  g_idt_p.limit = (sizeof(idt_entry) * 256) - 1;
  g_idt_p.base = (size_t)g_idt;

  // Clear the IDT.
  memset((char*)g_idt, 0, sizeof(idt_entry) * 256);

  // Load the new IDT pointer, which is in virtual address space.
  LoadIdt();
}

void LoadIdt() { __asm__ __volatile__("lidt %0" : : "m"(g_idt_p)); }

void SetIdtEntry(unsigned char num, size_t handler_address, unsigned short sel,
                 unsigned char flags, unsigned char ist) {
  g_idt[num].base_low = (handler_address & 0xFFFF);
  g_idt[num].base_middle = (handler_address >> 16) & 0xFFFF;
  g_idt[num].base_high = (handler_address >> 32) & 0xFFFFFFFF;

  g_idt[num].sel = sel;
  g_idt[num].always0 = ist & 0x7;
  g_idt[num].flags = flags;
  g_idt[num].zero = 0;
}

}  // namespace interrupts

#endif // TEST
