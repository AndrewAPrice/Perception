; Copyright 2021 Google LLC
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

[GLOBAL fiber_single_parameter_entrypoint]
[GLOBAL switch_with_fiber]
[GLOBAL jump_to_fiber]

; The fiber entrypoint that jumps to a function that takes a single parameter.
; We pass the parameters on the stack when we create a fiber, so we need to convert
; it to the System V calling convention.
fiber_single_parameter_entrypoint:
	; Get the function parameter
	mov rdi, [rsp + 8]
	; Jump to the function
	ret

; Switches context with a different fiber.
; rdi = new context
; rsi = old context
switch_with_fiber:
	; Remember stack
	mov rax, rsp

	; Point stack to top of context.
	add rsi, 8 * 7
	mov rsp, rsi

	; Push old stack
	push rax

	; Callee preserved registers
	push rbp
	push rbx
	push r12
	push r13
	push r14
	push r15

	; Fall through to jump_to_fiber

; Jumps to a different fiber's context without saving the
; existing fider.
; rdi - new context
jump_to_fiber:
	; Point to the bottom of the new context.
	mov rsp, rdi

	; Load callee-preserved registers
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbx
	pop rbp

	; Pop old stack
	pop rsp

	; Jump into the call location at the top of the stack.
	ret
