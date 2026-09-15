ENTRY(EntryPoint)

/* The kernel's code sits at 2GB below the end of the 64-bit memory range. */
VIRT_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
  /* The boot loader loads us at 1MB from the bottom of the memory. */
  . = 0x100000;

  /* The startup assembly code that lives in boot.asm. */
  .boot :
  {
    *(.mbhdr)
    loadStart = .;
    *(.boot)
    . = ALIGN(4096);
    Pml4 = .;
    . += 0x1000;
    Pdpt = .;
    . += 0x1000;
    Pd = .;
    . += 0x1000;
    . += 0x8000;
    Stack = .;
  }

  . += VIRT_BASE;

  /* The rest of the kernel's code goes below. */

  .text ALIGN(0x1000) : AT(ADDR(.text) - VIRT_BASE)
  {
    *(.text)
    *(.gnu.linkonce.t*)
  }
  
  .data ALIGN(0x1000) : AT(ADDR(.data) - VIRT_BASE)
  {
    *(.data)
    *(.gnu.linkonce.d*)
  }
  
  .rodata ALIGN(0x1000) : AT(ADDR(.rodata) - VIRT_BASE)
  {
    *(.rodata*)
    *(.gnu.linkonce.r*)
  }

  /* The size of the kernel's code. */
  _loadEnd = . - VIRT_BASE;
  
  .bss ALIGN(0x1000) : AT(ADDR(.bss) - VIRT_BASE)
  {
    *(COMMON)
    *(.bss*)
    *(.gnu.linkonce.b*)
  }
  
  bssEnd = . - VIRT_BASE;
  
  /DISCARD/ :
  {
    *(.comment)
    *(.eh_frame)
  }
}