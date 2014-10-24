[BITS 64]

[GLOBAL irq0]
[GLOBAL irq1]
[GLOBAL irq2]
[GLOBAL irq3]
[GLOBAL irq4]
[GLOBAL irq5]
[GLOBAL irq6]
[GLOBAL irq7]
[GLOBAL irq8]
[GLOBAL irq9]
[GLOBAL irq10]
[GLOBAL irq11]
[GLOBAL irq12]
[GLOBAL irq13]
[GLOBAL irq14]
[GLOBAL irq15]

irq0:
	cli
	push byte 0
	push byte 32
	jmp irq_common_stub

irq1:
	cli
	push byte 0
	push byte 33
	jmp irq_common_stub

irq2:
	cli
	push byte 0
	push byte 34
	jmp irq_common_stub

irq3:
	cli
	push byte 0
	push byte 35
	jmp irq_common_stub

irq4:
	cli
	push byte 0
	push byte 36
	jmp irq_common_stub

irq5:
	cli
	push byte 0
	push byte 37
	jmp irq_common_stub

irq6:
	cli
	push byte 0
	push byte 38
	jmp irq_common_stub

irq7:
	cli
	push byte 0
	push byte 39
	jmp irq_common_stub

irq8:
	cli
	push byte 0
	push byte 40
	jmp irq_common_stub

irq9:
	cli
	push byte 0
	push byte 41
	jmp irq_common_stub

irq10:
	cli
	push byte 0
	push byte 42
	jmp irq_common_stub

irq11:
	cli
	push byte 0
	push byte 43
	jmp irq_common_stub

irq12:
	cli
	push byte 0
	push byte 44
	jmp irq_common_stub

irq13:
	cli
	push byte 0
	push byte 45
	jmp irq_common_stub

irq14:
	cli
	push byte 0
	push byte 46
	jmp irq_common_stub

irq15:
	cli
	push byte 0
	push byte 47
	jmp irq_common_stub

[EXTERN irq_handler]
irq_common_stub:
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
	mov rax, irq_handler
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
