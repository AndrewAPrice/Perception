#ifdef X86
#include "turkey.h"

/*
	returns a function
	that function is a call stub that either jumps to the function or converts the calling convention to a native native function

	input parameters: rcx: ArgC, rdx: ArgT
	call native: Function(rcx: VM, rdx: Closure, r8: ArgC, r9: ArgT, stack: ...)
	call managed: Function(rbx: Closure, rcx: ArgC, rdx: ArgT, ...)

	rax: return value
	rbx: return type
	rcx, rdx: trashed - save all other values
*/
void *turkey_functionpointer_generate_call_stub(TurkeyVM *vm, TurkeyFunctionPointer *func_ptr) {
	if(func_ptr->is_native) {
		TurkeyAssembler asm;

		/* increase stack enough to store volatine registers in, rounded down to nearest -> store temporarily in RAX */
		asm.MoveRegisterToRegister(TURKEY_ASM_REGISTER_RSI, TURKEY_ASM_REGISTER_RAX);
		asm.SubtractValueFromRegister((10 + 4) * 8, TURKEY_ASM_REGISTER_RAX); /* 10 registers for temp registers + 4 values for backing store*/
		asm.MoveValueToRegister(0xFFFFFFFFFFFFFFF0, TURKEY_ASM_REGISTER_RBX);
		asm.BinaryAndValueWithRegister(TURKEY_ASM_REGISTER_RBX, TURKEY_ASM_REGISTER_RAX); /* align to 16 bytes */

		/* store volatine registers into stack,
		may be trashed by the callee: r8, r9, r10, r11, xmm0, xmm1, xmm2, xmm3, xmm4, xmm5 - 10 registers */

		/* move RAX into RSI to set up the new stack */
		asm.MoveRegisterToRegister(TURKEY_ASM_REGISTER_RAX, TURKEY_ASM_REGISTER_RSI);

		/* move parameters into their correct positions */
		asm.MoveRegisterToRegister(TURKEY_ASM_REGISTER_RCX, TURKEY_ASM_REGISTER_R8);
		asm.MoveRegisterToRegister(TURKEY_ASM_REGISTER_RDX, TURKEY_ASM_REGISTER_R9);
		asm.MoveValueToRegister((size_t)vm, TURKEY_ASM_REGISTER_RCX);
		asm.MoveValueToRegister((size_t)func_ptr->native.closure, TURKEY_ASM_REGISTER_RDX);

		/* call the native function */
		asm.Call(func_ptr->native.function);

		/* move return value (TurkeyVariable) out into rax, rbx */
		asm.MoveValueAtRegisterPlusOffsetToRegister(TURKEY_ASM_REGISTER_RAX, 8, TURKEY_ASM_REGISTER_RBX);
		asm.MoveValueAtRegisterToRegister(TURKEY_ASM_REGISTER_RAX, TURKEY_ASM_REGISTER_RAX);

		/* restore old registers from stack */

		/* move stack pointer back up */

		func_ptr->call_stub = asm.done();
	} else {
		TurkeyAssembler asm;

		/* move the closure value into rbx */
		asm.MoveValueToRegister((size_t)func_ptr->managed.closure, TURKEY_ASM_REGISTER_RBX);

		/* tail call into the entry point */
		asm.Jump(func_ptr->function->entry);
		
		func_ptr->call_stub = asm.done();
	}
}

/* returns a function
   that function converts native to managed calling convention
   
   input parameters: Function, Closure, ArgC, ArgT, ...
   call managed: Function(Closure, ArgC, ArgT, ...) */
void *turkey_function_call_managed_to_native(TurkeyVM *vm) {
	return 0;
}

#endif
