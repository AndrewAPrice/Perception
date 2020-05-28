[BITS 64]

[GLOBAL syscall_entry]
[EXTERN SyscallHandler]

; temp location to put the stack pointer while we manipulate the code
sys_call_stack_pointer:
    DQ 0x0000000000000000 

[EXTERN currently_executing_thread_regs]
[EXTERN interrupt_stack_top]

syscall_entry:
    ; Temporarily save userland rsp
    mov [sys_call_stack_pointer], rsp

    ; Store the current registers
    mov rsp, [currently_executing_thread_regs]
    add rsp, 19 * 8 ; point to usersp, skipping ss

    ; Push the registers
    push qword [sys_call_stack_pointer] ; usersp
    push r11 ; syscall puts rflags are in r11
    sub rsp, 8 ; skip cs
    push rcx ; syscall puts rip in rcx
    push rbp
    push rdi
    push rax
    push rbx
    sub rsp, 8 ; don't care about rcx, it was lost in the syscall
    push rdx
    push rsi
    push r8
    push r9
    push r10
    sub rsp, 8; don't care about r11, it was lost in the syscall
    push r12
    push r13
    push r14
    push r15

    ; Move to kernel land data segment - maybe not needed?
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    ;mov fs, ax
    ;mov gs, ax

    ; Move to the interrupt's stack.
    mov rsp, interrupt_stack_top

    ; Call the handler
    mov rax, SyscallHandler
    call rax

    ; Restore the registers of the caller - maybe not needed?
    mov ax, 0x18 | 3
    mov ds, ax
    mov es, ax
    ;mov fs, ax
    ;mov gs, ax

    mov rsp, [currently_executing_thread_regs]
    pop r15
    pop r14
    pop r13
    pop r12
    add rsp, 8 ; skip r11, it was lost in the syscall
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdx
    add rsp, 8 ; skip rcx, it was lost in the syscall
    pop rbx
    pop rax
    pop rdi
    pop rbp
    pop rcx ; pop rip into rcx
    add rsp, 8 ; skip cs
    pop r11 ; pop rflags into r11
    pop rsp

    o64 sysret