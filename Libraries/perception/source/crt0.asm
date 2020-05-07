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
[GLOBAL crt0_entry]
[extern main]
[extern start_ctors]
[extern end_ctors]
[extern start_dtors]
[extern end_dtors]
[extern __cxa_finalize]

; This is the entrypoint where programs begin running.
crt0_entry:
	; Call the global constructors.
	mov rbx, start_ctors
	jmp .checkIfWeHaveAGlobalConstructor
.callGlobalConstructor:
	call [rbx]
	add rbx, 8
.checkIfWeHaveAGlobalConstructor:
	cmp rbx, end_ctors
	jb .callGlobalConstructor

	; Call the C entry point.
	call main

	; Call registered destructors.
	mov rdi, 0
	call __cxa_finalize

	; Call the global destructors.
	mov rbx, end_dtors
	jmp .checkIfWeHaveAGlobalDestructor
.callGlobalDestructor:
	sub rbx, 8
	call [rbx]
.checkIfWeHaveAGlobalDestructor:
	cmp rbx, start_dtors
	ja .callGlobalDestructor

	; Terminate the process if main returns.
	mov rdi, 6
	syscall
