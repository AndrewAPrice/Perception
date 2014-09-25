[BITS 32]
[SECTION .boot]
[GLOBAL EntryPoint]
[EXTERN Stack]
EntryPoint:
	mov [MultibootInfo], eax
	mov [MultibootInfo + 4], ebx

	mov eax, Gdtr1
	lgdt [eax]

	push 0x08
	push .GdtReady
	retf

.GdtReady:
	mov eax, 0x10
	mov ds, ax
	mov ss, ax
	mov esp, Stack

	call SetupPagingAndLongMode

	mov eax, Gdtr2
	lgdt [Gdtr2]

	push 0x08
	push Gdt2Ready
	retf

[EXTERN Pml4]
[EXTERN Pdpt]
[EXTERN Pd]
SetupPagingAndLongMode:
	mov eax, Pdpt
	or eax, 1
	mov [Pml4], eax
	mov [Pml4 + 0xFF8], eax

	mov eax, Pd
	or eax, 1
	mov [Pdpt], eax
	mov [Pdpt + 0xFF0], eax

	mov dword [Pd], 0x000083
	mov dword [Pd + 8], 0x200083
	mov dword [Pd + 16], 0x400083
	mov dword [Pd + 24], 0x600083

	; Load CR3 with PML4
	mov eax, Pml4
	mov cr3, eax

	; Enable PAE
	mov eax, cr4
	or eax, 1 << 5
	mov cr4, eax

	; Enable Load Mode in the MSR
	mov ecx, 0xC0000080
	rdmsr
	or eax, 1 << 8
	wrmsr

	; Enable paging
	mov eax, cr0
	or eax, 1 << 31
	mov cr0, eax

	ret

TmpGdt:
	DQ 0x0000000000000000
	DQ 0x00CF9A000000FFFF
	DQ 0x00CF92000000FFFF
	DQ 0x0000000000000000
	DQ 0x00A09A0000000000
	DQ 0x00A0920000000000

Gdtr1:
	DW 23
	DD TmpGdt

Gdtr2:
	DW 23
	DD TmpGdt + 24
	DD 0

Gdtr3:
	DW 23
	DQ TmpGdt + 24 + 0xFFFFFFFF80000000

[GLOBAL MultibootInfo]
MultibootInfo:
	dq 0 ; pointer and magic stuff from grub written here

[BITS 64]
[EXTERN kmain]
Gdt2Ready:
	mov eax, 0x10
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov rsp, Stack + 0xFFFFFFFF80000000

	; mov gdtr to the upper zone
	mov rax, Gdtr3
	lgdt [rax]

	mov rax, kmain
	call rax
	cli
	hlt
	jmp $
