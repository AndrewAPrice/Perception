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
	push 0
	jmp irq_common_stub

irq1:
	cli
	push 1
	jmp irq_common_stub

irq2:
	cli
	push 2
	jmp irq_common_stub

irq3:
	cli
	push 3
	jmp irq_common_stub

irq4:
	cli
	push 4
	jmp irq_common_stub

irq5:
	cli
	push 5
	jmp irq_common_stub

irq6:
	cli
	push 6
	jmp irq_common_stub

irq7:
	cli
	push 7
	jmp irq_common_stub

irq8:
	cli
	push 8
	jmp irq_common_stub

irq9:
	cli
	push 9
	jmp irq_common_stub

irq10:
	cli
	push 10
	jmp irq_common_stub

irq11:
	cli
	push 11
	jmp irq_common_stub

irq12:
	cli
	push 12
	jmp irq_common_stub

irq13:
	cli
	push 13
	jmp irq_common_stub

irq14:
	cli
	push 14
	jmp irq_common_stub

irq15:
	cli
	push 15
	jmp irq_common_stub

[EXTERN CommonHardwareInterruptHandler]
[EXTERN currently_executing_thread_regs]
[EXTERN interrupt_stack_top]
irq_common_stub:
	; Copy what's at the top of the thread's stack.
	push rbp
	push rdi ; Using to keep the interrupt number.

	; Move these values out of the interrupt handler
	mov rbp, [currently_executing_thread_regs]
	pop qword [rbp + 13 * 8] ; rdi
	pop qword [rbp + 14 * 8] ; rbp
	pop qword rdi ; interrupt number
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

	; Call the handler
	; mov rdi, [rbp - 46] ; pass interrupt number as argument
	; Interrupt number is in rdi and will get passed as an argument.
	mov rax, CommonHardwareInterruptHandler
	call rax

[GLOBAL JumpIntoThread]
JumpIntoThread:
	; Move back to userland data segment.
	mov ax, 0x18 | 3
	mov ds, ax
	mov es, ax

 	; Jump to the bottom of isr_regs, which contains 20 64-bit registers.
	mov rsp, [currently_executing_thread_regs]

	; Pop the registers into memory.
	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rsi
	pop rdx
	pop rcx
	pop rbx
	pop rax
	pop rdi
	pop rbp
	iretq ; pops RIP, CS, EFLAGS, RSP, SS