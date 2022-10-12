; Copyright 2020 Google LLC
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

[BITS 64]
[GLOBAL isr0]
[GLOBAL isr1]
[GLOBAL isr2]
[GLOBAL isr3]
[GLOBAL isr4]
[GLOBAL isr5]
[GLOBAL isr6]
[GLOBAL isr7]
[GLOBAL isr8]
[GLOBAL isr9]
[GLOBAL isr10]
[GLOBAL isr11]
[GLOBAL isr12]
[GLOBAL isr13]
[GLOBAL isr14]
[GLOBAL isr15]
[GLOBAL isr16]
[GLOBAL isr17]
[GLOBAL isr18]
[GLOBAL isr19]
[GLOBAL isr20]
[GLOBAL isr21]
[GLOBAL isr22]
[GLOBAL isr23]
[GLOBAL isr24]
[GLOBAL isr25]
[GLOBAL isr26]
[GLOBAL isr27]
[GLOBAL isr28]
[GLOBAL isr29]
[GLOBAL isr30]
[GLOBAL isr31]

;  0: Divide By Zero Exception
isr0:
    cli
    push 0
    push 0
    jmp exception_common_stub

;  1: Debug Exception
isr1:
    cli
    push 0
    push 1
    jmp exception_common_stub

;  2: Non Maskable Interrupt Exception
isr2:
    cli
    push 0
    push 2
    jmp exception_common_stub

;  3: Int 3 Exception
isr3:
    cli
    push 0
    push 3
    jmp exception_common_stub

;  4: INTO Exception
isr4:
    cli
    push 0
    push 4
    jmp exception_common_stub

;  5: Out of Bounds Exception
isr5:
    cli
    push 0
    push 5
    jmp exception_common_stub

;  6: Invalid Opcode Exception
isr6:
    cli
    push 0
    push 6
    jmp exception_common_stub

;  7: Coprocessor Not Available Exception
isr7:
    cli
    push 0
    push 7
    jmp exception_common_stub

;  8: Double Fault Exception (With Error Code!)
isr8:
    cli
    push 8
    jmp exception_common_stub

;  9: Coprocessor Segment Overrun Exception
isr9:
    cli
    push 0
    push 9
    jmp exception_common_stub

; 10: Bad TSS Exception (With Error Code!)
isr10:
    cli
    push 10
    jmp exception_common_stub

; 11: Segment Not Present Exception (With Error Code!)
isr11:
    cli
    push 11
    jmp exception_common_stub

; 12: Stack Fault Exception (With Error Code!)
isr12:
    cli
    push 12
    jmp exception_common_stub

; 13: General Protection Fault Exception (With Error Code!)
isr13:
    cli
    push 13
    jmp exception_common_stub

; 14: Page Fault Exception (With Error Code!)
isr14:
    cli
    push 14
    jmp exception_common_stub

; 15: Reserved Exception
isr15:
    cli
    push 0
    push 15
    jmp exception_common_stub

; 16: Floating Point Exception
isr16:
    cli
    push 0
    push 16
    jmp exception_common_stub

; 17: Alignment Check Exception
isr17:
    cli
    push 0
    push 17
    jmp exception_common_stub

; 18: Machine Check Exception
isr18:
    cli
    push 0
    push 18
    jmp exception_common_stub

; 19: Reserved
isr19:
    cli
    push 0
    push 19
    jmp exception_common_stub

; 20: Reserved
isr20:
    cli
    push 0
    push 20
    jmp exception_common_stub

; 21: Reserved
isr21:
    cli
    push 0
    push 21
    jmp exception_common_stub

; 22: Reserved
isr22:
    cli
    push 0
    push 22
    jmp exception_common_stub

; 23: Reserved
isr23:
    cli
    push 0
    push 23
    jmp exception_common_stub

; 24: Reserved
isr24:
    cli
    push 0
    push 24
    jmp exception_common_stub

; 25: Reserved
isr25:
    cli
    push 0
    push 25
    jmp exception_common_stub

; 26: Reserved
isr26:
    cli
    push 0
    push 26
    jmp exception_common_stub

; 27: Reserved
isr27:
    cli
    push 0
    push 27
    jmp exception_common_stub

; 28: Reserved
isr28:
    cli
    push 0
    push 28
    jmp exception_common_stub

; 29: Reserved
isr29:
    cli
    push 0
    push 29
    jmp exception_common_stub

; 30: Reserved
isr30:
    cli
    push 0
    push 30
    jmp exception_common_stub

; 31: Reserved
isr31:
    cli
    push 0
    push 31
    jmp exception_common_stub

[EXTERN ExceptionHandler]
[EXTERN currently_executing_thread_regs]
[EXTERN interrupt_stack_top]
[EXTERN JumpIntoThread]
exception_common_stub:
    ; Copy what's at the top of the thread's stack.
    push rbp
    push rdi ; Using to keep the interrupt number.

    ; Move these values out of the interrupt handler
    mov rbp, [currently_executing_thread_regs]
    pop qword [rbp + 13 * 8] ; rdi
    pop qword [rbp + 14 * 8] ; rbp
    pop rdi ; interrupt number
    pop qword [rbp + 15 * 8] ; rip
    pop qword [rbp + 16 * 8] ; cs
    pop qword [rbp + 17 * 8] ; eflags
    pop qword [rbp + 18 * 8] ; usersp
    pop qword [rbp + 19 * 8] ; ss

    ; Point our stack to the top of isr_regs, minus the registers already saved.
    mov rsp, [currently_executing_thread_regs]
    add rsp, 13 * 8

    ; Push the rest of the registers.
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Move back to the interrupt's stack.
    mov rsp, [interrupt_stack_top]

    ; Move to kernel land data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax

    ; rdi is already populated with the interrupt number, and we can pass
    ; in additional arguments to the handler.
    mov rsi, cr2

    ; Call the handler
    ; mov rdi, [rbp - 46] ; pass interrupt number as argument
    ; Interrupt number is in rdi and will get passed as an argument.
    mov rax, ExceptionHandler
    call rax
    jmp JumpIntoThread