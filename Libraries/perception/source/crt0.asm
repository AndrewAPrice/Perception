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

; This is the entrypoint where programs begin running.
crt0_entry:
	; Call the global constructors.
	;call call_global_constructors

	; Call the C entry point.
	call main

	; Call the global destructors.
	;call call_global_destructors

	; Terminate the process if main returns.
	mov rdi, 6
	syscall

[SECTION .preconstructors]
.call_global_constructors:
	push rbp
	mov rbp, rsp
	; The linker will fill in here.

[SECTION .postconstructors]
	pop rbp
	ret

[SECTION .predestructors]
.call_global_destructors:
	push rbp
	mov rbp, rsp
	; The linker will fill in here.

[SECTION .postdestructors]
	pop rbp
	ret
