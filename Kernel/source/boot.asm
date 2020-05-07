[BITS 32]

; This code lives at the bottom of memory, because it's executed before paging is set up, unlike
; the rest of the kernel that lives at the top of memory.
[SECTION .boot]

; The multiboot boot loader starts executing the kernel from here.
[GLOBAL EntryPoint]
[EXTERN Stack]
EntryPoint:
	; Save the pointer to the multiboot info, so we can later read it in C.
	mov [MultibootInfo], eax
	mov [MultibootInfo + 4], ebx

	; Load our 32-bit Global Descriptor Table.
	lgdt [Gdtr32]
	
	; Jump to .GdtReady in our executable segment.
	push 0x08
	push GdtReady
	retf

; In our 32-bit executable segment.
[GLOBAL GdtReady]
GdtReady:
	; Set our data and stack registers to point to the data segment.
	mov eax, 0x10
	mov ds, ax
	mov ss, ax

	; Sets the stack.
	mov esp, Stack

	; Call the function below to set up long mode paging.
	call SetupPagingAndLongMode

	; Load our 64-bit GDT, in lower memory.
	mov eax, GdtrLower64
	lgdt [GdtrLower64]

	; Jump to long-mode.
	push 0x08
	push Gdt64Ready
	retf

; Sets up the initial paging and long mode.
[EXTERN Pml4]
[EXTERN Pdpt]
[EXTERN Pd]
SetupPagingAndLongMode:
	; We'll set up a temporary paging system, where we map the first 8 MB of physical memory to the first
	; 8 MB of lower and upper virtual memory. (Actually, it gets mapped 4 times, so we can reuse the same
	; PD, but we can ignore that.) Once we enter C land, the kernel will set up a better paging system.

	; The PML4 is our root table. Point the first 512GB TB in lower and upper memory to the same PDPT.
	mov eax, Pdpt
	or eax, 3 ; present, RW
	mov [Pml4], eax
	mov [Pml4 + 0xFF8], eax ; Last entry.

	; Point the 0->1GB GB in lower and 2->3 GB in upper memory to the same PD.
	mov eax, Pd
	or eax, 3 ; present, RW
	mov [Pdpt], eax
	mov [Pdpt + 0xFF0], eax ; Second the last entry.

	; Map the first 8 MB of physical memory to the first 8 MB of virtual memory in the PD.
	mov dword [Pd], 0x000083 ; 0 -> 2 MB, 2 MB page, RW, supervisor page
	mov dword [Pd + 8], 0x200083 ; 2 -> 4MB, 2 MB page, RW, supervisor page
	mov dword [Pd + 16], 0x400083 ; 4 -> 6 MB, 2 MB page, RW, supervisor page
	mov dword [Pd + 24], 0x600083 ; 6 -> 8 MB, 2 MB page, RW, supervisor page

	; Load CR3 with PML4.
	mov eax, Pml4
	mov cr3, eax

	; Enable PAE (5) and OSFXSR (9) and OSXMMEXCPT (10) for FPU.
	mov eax, cr4
	or eax, (1 << 5) | (1 << 9) | (1 << 10)
	mov cr4, eax

	; Enable Load Mode (8) and System Call Extensions (0) in the MSR.
	mov ecx, 0xC0000080
	rdmsr
	or eax, (1 << 8) | (1)
	wrmsr

	; Enable paging (31) and MP (1) for FPU.
	mov eax, cr0
	or eax, (1 << 31) | (1 << 1)

	; Clear EM (2) for FPU.
	and eax, ~(1 << 2)
	mov cr0, eax

	ret

; 32-bit Global Descriptor Table, it tells us about the memory segments. It maps all of the
; 32-bit address field as executable and data.
[GLOBAL Gdt32]
Gdt32:
	; Invalid segment
	DQ 0x0000000000000000
	; 0->4GB is RW, executable, code/data segment, present, 4k, 32-bit
	DQ 0x00CF9A000000FFFF
	; 0->4GB is RW, data, code/data segment, present, 4k, 32-bit
	DQ 0x00CF92000000FFFF
; 64-bit Global Descriptor Table. It must be defined, but segments map to all of memory in long mode.
[GLOBAL Gdt64]
Gdt64:
	; Invalid segment
	DQ 0x0000000000000000 ; 0x0
	; Kernel code: RW, executable, code/data segment, present, 64-bit, ring 0
	DQ 0x00209A0000000000 ; 0x8
	; Kernel data: RW, data, code/data segment, present, ring 0
	DQ 0x0000920000000000 ; 0x10
	; User data: RW, data, code/data segment, present, ring 3
	DQ 0x0020F20000000000 ; 0x18
	; User code: RW, executable, code/data segment, present, 64-bit, ring 3
	DQ 0x0020FA0000000000 ; 0x20
	; TSS. It takes up 2 entries.
[GLOBAL TSSEntry]
TSSEntry:
	DQ 0x0000000000000000 ; 0x28
	DQ 0x0000000000000000

; Reference to the 32-bit GDT.
Gdtr32:
	DW 23 ; 24 bytes long
	DD Gdt32

; Reference to the 64-bit global descriptor table, in lower memory.
GdtrLower64:
	DW 23 ; 24 bytes long, we'll ignore the user segments
	DD Gdt64
	DD 0

; Reference to the 64-bit global descriptor table, in upper memory.
GdtrUpper64:
	DW 55 ; 56 bytes long
	DQ Gdt64 + 0xFFFFFFFF80000000

[GLOBAL MultibootInfo]
MultibootInfo:
	dq 0 ; pointer and magic stuff from grub written here

[BITS 64]
; The entry point in long mode.
[EXTERN kmain]
[GLOBAL Gdt64Ready]
Gdt64Ready:
	; Turn off interrupts. Is this needed?
	cli

	; Once the kernel sets up paging, we'll loose the stuff in 'low memory' (since that is then going to
	; be userland memory), so we need to move the stack and GDTR to upper memory.

	; Move our stack into upper memory.
	mov rsp, Stack + 0xFFFFFFFF80000000
	mov rbp, rsp

	; Load the upper memory GDT.
	mov rax, GdtrUpper64 + 0xFFFFFFFF80000000
	lgdt [rax]

	; Point our data segments to the data segment.
	mov rax, 0x10
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

	; Jump to the C entry point.
	mov rax, kmain
	jmp rax