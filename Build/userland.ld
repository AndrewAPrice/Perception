/*
Copyright 2020 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

ENTRY(crt0_entry)

SECTIONS
{
  . = 0x000000;

  .text ALIGN(0x1000) : AT(ADDR(.text))
  {
    *(.text*)
    *(.gnu.linkonce.t*)
  }
  
  .data ALIGN(0x1000) : AT(ADDR(.data))
  {
    *(.data*)
    *(.gnu.linkonce.d*)
  }
  
  .rodata ALIGN(0x1000) : AT(ADDR(.rodata))
  {
    __init_array_start = .;
    KEEP(*(SORT(.ctors*)))
    __init_array_end = .;

    start_dtors = .;
    KEEP(*(SORT(.dtors*)))
    end_dtors = .;

    *(.rodata*)
    *(.gnu.linkonce.r*)
  }

  .bss ALIGN(0x1000) : AT(ADDR(.bss))
  {
    *(COMMON)
    *(.bss*)
    *(.gnu.linkonce.b*)
  }

  .eh_frame_hdr : { *(.eh_frame_hdr) }
  .eh_frame : { *(.eh_frame) }
  .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) }
  .exception_ranges : { *(.exception_ranges .exception_ranges*) }
  
  bssEnd = .;
  
  /DISCARD/ :
  {
    *(.comment)
  }
}