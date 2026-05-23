.text
.global __cp_begin
.hidden __cp_begin
.global __cp_end
.hidden __cp_end
.global __cp_cancel
.hidden __cp_cancel
.hidden __cancel
.global __syscall_cp_asm
.hidden __syscall_cp_asm
.type   __syscall_cp_asm,@function
__syscall_cp_asm:

__cp_begin:
	mov (%rdi),%eax
	test %eax,%eax
	jnz __cp_cancel

	push %rbp
	mov %rsp,%rbp

	# Align stack to 16 bytes by pushing 8 bytes of padding first,
	# then the 7th argument z.
	push $0
	push 24(%rbp)

	mov %r8,%r10
	mov %r9,%r8
	mov 16(%rbp),%r9

	mov %rcx,%rax
	mov %r10,%rcx

	mov %rdx,%r10
	mov %rax,%rdx

	mov %rsi,%rdi
	mov %r10,%rsi

	call __syscall6

	leave
__cp_end:
	ret
__cp_cancel:
	jmp __cancel
