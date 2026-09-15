; Copyright 2026 Google LLC
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;      http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

[SECTION .text]
[GLOBAL ap_trampoline_start]
[GLOBAL ap_trampoline_end]
[EXTERN ApMain]

align 16
[BITS 16]
ap_trampoline_start:
    jmp short ap_start
    align 4

ap_pml4:
    dd 0
ap_stack:
    dq 0
ap_core_id:
    dd 0
ap_status:
    dd 0

align 16
ap_start:
    cli
    cld

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov dword [0x8000 + (ap_status - ap_trampoline_start)], 1

    lgdt [0x8000 + (ap_gdtr - ap_trampoline_start)]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x18:(0x8000 + (ap_pm_entry - ap_trampoline_start))

[BITS 32]
ap_pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov dword [0x8000 + (ap_status - ap_trampoline_start)], 2

    ; Enable PAE (bit 5), PGE (bit 7), OSFXSR (bit 9), OSXMMEXCPT (bit 10) in CR4.
    mov eax, cr4
    or eax, (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10)
    mov cr4, eax

    ; Load CR3 with PML4 address provided in ap_pml4.
    mov eax, [0x8000 + (ap_pml4 - ap_trampoline_start)]
    mov cr3, eax

    ; Enable Long Mode (bit 8) and NXE (bit 11) in EFER MSR.
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8) | (1 << 11) | 1
    wrmsr

    ; Enable Paging (bit 31) and MP (bit 1) and PE (bit 0) in CR0.
    mov eax, cr0
    or eax, (1 << 31) | (1 << 1) | 1
    and eax, ~(1 << 2)
    mov cr0, eax

    ; Jump to 64-bit long mode with selector 0x08.
    jmp 0x08:(0x8000 + (ap_lm_entry - ap_trampoline_start))

[BITS 64]
ap_lm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov rbx, 0x8000
    mov dword [rbx + (ap_status - ap_trampoline_start)], 3

    ; Load this AP's initial stack pointer.
    mov rsp, [rbx + (ap_stack - ap_trampoline_start)]

    ; Load logical core ID argument into RDI for ApMain(size_t core_id).
    mov edi, [rbx + (ap_core_id - ap_trampoline_start)]

    ; Call 64-bit C++ entry point in kernel upper memory.
    mov rax, ApMain
    call rax

.ap_halt_loop:
    cli
    hlt
    jmp .ap_halt_loop

align 8
ap_gdtr:
    dw 31 ; 4 entries * 8 bytes - 1
    dd 0x8000 + (ap_gdt - ap_trampoline_start)

align 8
ap_gdt:
    dq 0x0000000000000000 ; 0x00: Null descriptor
    dq 0x00209A0000000000 ; 0x08: 64-bit Code descriptor (DPL 0, Long mode)
    dq 0x00CF92000000FFFF ; 0x10: Data descriptor (DPL 0, Limit 4GB)
    dq 0x00CF9A000000FFFF ; 0x18: 32-bit Code descriptor (DPL 0, Limit 4GB, 32-bit default)

ap_trampoline_end:
