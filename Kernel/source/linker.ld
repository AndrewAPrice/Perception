ENTRY(EntryPoint)

VIRT_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
  . = 0x100000;

  .boot :
  {
    *(.mbhdr)
    _loadStart = .;
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
  
  _loadEnd = . - VIRT_BASE;
  
  .bss ALIGN(0x1000) : AT(ADDR(.bss) - VIRT_BASE)
  {
    *(COMMON)
    *(.bss)
    *(.gnu.linkonce.b*)
  }
  
  _bssEnd = . - VIRT_BASE;
  
  /DISCARD/ :
  {
    *(.comment)
    *(.eh_frame)
  }
}
