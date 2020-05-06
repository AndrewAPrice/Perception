[BITS 64]

[GLOBAL syscall_isr]
[EXTERN syscall_handler]

syscall_isr:
	cli
	push byte 0
	push byte 0
	push rax
	push rbx
	push rcx
	push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

	mov ax, 0x8
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	mov rdi, rsp ; pass as argument
	mov rax, syscall_handler
	call rax
	mov rsp, rax ; returns a pointer to the stack

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
	add rsp, 16 ; Clean up the pushed error code and pushed ISR number
	iretq ; pops RIP, CS, EFLAGS, RSP, SS
