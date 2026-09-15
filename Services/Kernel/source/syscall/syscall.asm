[BITS 64]

[GLOBAL syscall_entry]
[EXTERN ProfileEnteringKernelSpaceForSyscall]
[EXTERN g_profiling_enabling_count]
[EXTERN ProfileSwitchToUserSpace]
[EXTERN SyscallHandler]
[EXTERN RescheduleWithIretq]
[EXTERN JumpIntoThread]

syscall_entry:
    ; Swap to kernel GS base containing this core's CpuCoreState.
    swapgs

    ; Temporarily save userland rsp in per-CPU scratch storage (offset 8).
    mov [gs:8], rsp

    ; Store the current registers in this core's thread registers (offset 16).
    mov rsp, [gs:16]
    add rsp, 20 * 8 ; point to top of Registers struct

    ; Push the registers
    push qword 0x1b ; ss (kUserDataSelector | kUserRpl)
    push qword [gs:8] ; usersp
    push r11 ; syscall puts rflags are in r11
    push qword 0x23 ; cs (kUserCodeSelector | kUserRpl)
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

    ; Move to this core's interrupt stack (offset 24).
    mov rsp, [gs:24]

    ; Jump over profiling code if profiler isn't enabled.
    mov r8, [g_profiling_enabling_count]
    test r8, r8
    jz .jump_over_pre_handler_profiling

    ; Save parameters that are going to be passed to SyscallHandler.
    push rdi

    ; Call the profiler - rdi is already populated with the exception handler.
    call ProfileEnteringKernelSpaceForSyscall

    ; Restore parameters that are going to be passed to SyscallHandler.
    pop rdi

.jump_over_pre_handler_profiling:
    ; Call the handler
    mov rax, SyscallHandler
    call rax

    ; Check if to return via iretq
    mov rax, RescheduleWithIretq
    call rax
    test al, al
    jnz .return_via_iretq

    ; Jump over profiling code if profiler isn't enabled.
    mov r8, [g_profiling_enabling_count]
    test r8, r8
    jz .jump_over_post_handler_profiling

    ; Call the profiler - rdi is already populated with the exception handler.
    call ProfileSwitchToUserSpace

.jump_over_post_handler_profiling:
    mov rsp, [gs:16]
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
    ; rsp points to rip.
    ; Check if the segment selector cs (at rsp + 8) is user mode (privilege level 3).
    test qword [rsp + 8], 3
    jz .return_to_kernel

    ; Ensure user-mode RIP is canonical (bits 47..63 must be 0). If not canonical,
    ; sysret would cause a #GP in ring 0, so fall back to iretq.
    test dword [rsp + 4], 0xFFFF8000
    jnz .return_to_kernel

    pop rcx ; pop rip into rcx
    add rsp, 8 ; skip cs
    pop r11 ; pop rflags into r11
    pop rsp
    swapgs
    o64 sysret

.return_to_kernel:
    iretq

.return_via_iretq:
    jmp JumpIntoThread