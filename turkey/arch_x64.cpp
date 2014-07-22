/*
** This file has been pre-processed with DynASM.
** http://luajit.org/dynasm.html
** DynASM version 1.3.0, DynASM x64 version 1.3.0
** DO NOT EDIT! The original file is in "arch_x64.dasc".
*/

#include "turkey.h"
#include "external/dynasm/dasm_proto.h"
#include "external/dynasm/dasm_x86.h"

//| .arch x64
#if DASM_VERSION != 10300
#error "Version mismatch between DynASM and included encoding engine"
#endif
//|.actionlist actions
static const unsigned char actions[250] = {
  73,199,192,237,255,72,199,194,237,252,255,226,255,252,233,245,255,72,137,
  232,72,41,224,72,131,232,8,255,72,137,226,255,72,193,232,3,255,85,72,137,
  229,255,72,129,252,236,239,255,72,131,228,252,240,255,72,41,212,255,72,131,
  226,8,255,72,137,215,72,131,199,32,255,72,137,252,238,72,131,198,16,255,72,
  131,252,248,0,15,132,244,248,248,1,255,165,255,72,131,199,4,72,131,198,4,
  255,72,252,255,200,255,72,131,252,248,0,15,132,244,248,255,252,233,244,1,
  248,2,255,73,137,200,72,137,252,233,72,131,252,233,16,72,199,194,237,255,
  72,199,192,237,252,255,224,255,72,139,133,233,72,139,141,233,255,72,137,252,
  236,93,255,195,255,81,255,76,137,200,72,193,224,3,72,41,196,255,72,137,231,
  255,72,137,252,238,72,131,198,24,255,73,131,252,249,0,15,132,244,248,248,
  1,255,73,252,255,201,255,73,131,252,249,0,15,132,244,248,255,76,135,193,255,
  65,252,255,208,255,72,137,229,93,255,90,72,137,10,72,131,194,8,72,137,2,255,
  72,137,200,255
};


#define Dst &(vm->dasm_state)

void turkey_arch_initialize(TurkeyVM *vm) {
	dasm_init(Dst, 1);
	// dasm_setupglobal ?
	dasm_setup(Dst, actions);
}

void turkey_arch_cleanup(TurkeyVM *vm) {
	dasm_free(Dst);
}

void *turkey_arch_functionpointer_managed_thunk(TurkeyVM *vm, TurkeyFunctionPointer *func) {
	assert(func->is_native == false);

	// all parameters + return value are on the stack
	// types are in rcx
	// move closure into r8
	
	if(func->managed.closure) {
		//| mov r8, func->managed.closure
		dasm_put(Dst, 0, func->managed.closure);
	}

	// do a tail jump, returning straight to the parent
	size_t funcentry = (size_t)func->managed.function->entry_point;
	if(funcentry > 0xFFFFFFFF) {
		//| mov rdx, funcentry
		//| jmp rdx
		dasm_put(Dst, 5, funcentry);
	} else {
		//| jmp =>funcentry
		dasm_put(Dst, 13, funcentry);
	}

	// return value and type should be in rax, rcx
	size_t code_size;
	dasm_link(Dst, &code_size);
	void *mem = turkey_allocate_executable_memory(vm->tag, code_size);

	dasm_encode(Dst, mem);
	return mem;
}

void *turkey_arch_functionpointer_native_thunk(TurkeyVM *vm, TurkeyFunctionPointer *func) {
	assert(func->is_native == true);

	// all parameters + return value are on the stack
	// types are in rcx

	// calculate how many parameters we have into rax
	//| mov rax, rbp
	//| sub rax, rsp
	//| sub rax, 8
	dasm_put(Dst, 17);
	// store an unshifted copy into rdx
	//| mov rdx, rsp
	dasm_put(Dst, 28);
	// shift right to turn it into a count of parameters
	//| shr rax, 3
	dasm_put(Dst, 32);
	
	// enter new stack frame
	//| push rbp
	//| mov rbp, rsp
	dasm_put(Dst, 37);

	// subtract space for return value, plus also enough for the shadow space
	//| sub rsp, 16+(8*4)
	dasm_put(Dst, 42, 16+(8*4));

	// align to 16 bytes
	//| and rsp, -0x10
	dasm_put(Dst, 48);

	// subtract enough room on stack to shadow parameters
	//| sub rsp, rdx
	dasm_put(Dst, 54);
	// figure out if we have an odd number of parameters
	//| and rdx, 8
	dasm_put(Dst, 58);
	// subtract one more time (only does something if we had an odd number of parameters)
	//| sub rsp, rdx
	dasm_put(Dst, 54);
	
	// final stack should be:
	// 16 bytes for return value
	// optional 8 byte padding to get the stack 16 byte aligned
	// copy of the parameters
	// 4 * 8 byte shadow space

	// now copy the parameters right above the shadow space
	//| mov rdi, rdx
	//| add rdi, 32
	dasm_put(Dst, 63);
	// +16 jumps over the return value and pushed rbp
	//| mov rsi, rbp
	//| add rsi, 16
	dasm_put(Dst, 71);

	// skip over loop if we're down to 0
	//| cmp rax, 0
	//| je >2
	//|1:
	dasm_put(Dst, 80);
		// copy 4 bytes
		//| movsd
		dasm_put(Dst, 92);
		// shift up stack
		//| add rdi, 4
		//| add rsi, 4
		dasm_put(Dst, 94);
		// copy 4 bytes
		//| movsd
		dasm_put(Dst, 92);
		// decrement count
		//| dec rax
		dasm_put(Dst, 103);
		// shift up stack
		//| add rdi, 4
		//| add rsi, 4
		dasm_put(Dst, 94);
		// break loop if we're down to 0
		//| cmp rax, 0
		//| je >2
		dasm_put(Dst, 108);
		// loop
		//| jmp <1
	//|2:
	dasm_put(Dst, 118);
	
	// pointer to return space in rcx
	// move vm into rdx
	// move closure into r8
	// move types into r9
	//| mov r8, rcx
	//| mov rcx, rbp
	//| sub rcx, 16
	//| mov rdx, func->native.closure
	dasm_put(Dst, 125, func->native.closure);
	
	// reserve shadow space for parameters
	//| sub rsp, 4 * 8
	dasm_put(Dst, 42, 4 * 8);

	// call native code
	size funcentry = (size_t)func->native.function;
	if(funcentry > 0xFFFFFFFF) {
		//| mov rax, funcentry
		//| jmp rax
		dasm_put(Dst, 142, funcentry);
	} else {
		//| jmp =>funcentry
		dasm_put(Dst, 13, funcentry);
	}

	// extract our parameters (value, type)
	//| mov rax, [ebp - 8]
	//| mov rcx, [ebp - 16]
	dasm_put(Dst, 150, - 8, - 16);

	// return to our upper stack frame
	//| mov rsp, rbp
	//| pop rbp
	dasm_put(Dst, 159);

	// return to our parent
	//| ret
	dasm_put(Dst, 165);

	size_t code_size;
	dasm_link(Dst, &code_size);
	void *mem = turkey_allocate_executable_memory(vm->tag, code_size);

	dasm_encode(Dst, mem);
	return mem;
}

void *turkey_arch_native_to_managed_thunker(TurkeyVM *vm) {
	// returns a function pointer you can call from native code to call TurkeyFunctionPointers
	// call it as: - parameter 3 is not used
	// TurkeyVariable thunker(funcptr->thunk, types, parametercount, ...parameters)
	
	// input:
	// rcx - where to copy return value
	// rdx - pointer to the thunker
	// r8 - parameter types
	// r9 - number of parameters
	// parameters on stack + 4 * 8 byte shadow memory + return value

	// store rcx
	//| push rcx
	dasm_put(Dst, 167);
	// enter a new stack frame
	//| push rbp
	//| mov rbp, rsp
	dasm_put(Dst, 37);

	// make room on the stack for the parameters
	//| mov rax, r9
	//| shl rax, 3
	//| sub rsp, rax
	dasm_put(Dst, 169);

	// copy parameters from upper stack to our stack
	//| mov rdi, rsp
	dasm_put(Dst, 180);
	// +24 jumps over the return value and pushed rcx, rbp
	//| mov rsi, rbp
	//| add rsi, 24
	dasm_put(Dst, 184);

	// skip over loop if we're down to 0
	//| cmp r9, 0
	//| je >2
	//|1:
	dasm_put(Dst, 193);
		// copy 4 bytes
		//| movsd
		dasm_put(Dst, 92);
		// shift up stack
		//| add rdi, 4
		//| add rsi, 4
		dasm_put(Dst, 94);
		// copy 4 bytes
		//| movsd
		dasm_put(Dst, 92);
		// decrement count
		//| dec r9
		dasm_put(Dst, 205);
		// shift up stack
		//| add rdi, 4
		//| add rsi, 4
		dasm_put(Dst, 94);
		// break loop if we're down to 0
		//| cmp r9, 0
		//| je >2
		dasm_put(Dst, 210);
		// loop
		//| jmp <1
	//|2:
	dasm_put(Dst, 118);

	// move types into rcx (currently containing the thunk pointer)
	//| xchg r8, rcx
	dasm_put(Dst, 220);

	// call the thunk
	//| call r8
	dasm_put(Dst, 224);

	// return to the old stack frame
	//| mov rbp, rsp
	//| pop rbp
	dasm_put(Dst, 229);

	// copy return values from the thunk into the return struct (rdx was rcx coming in)
	//| pop rdx
	//| mov [rdx], rcx
	//| add rdx, 8
	//| mov [rdx], rax
	dasm_put(Dst, 234);
	// point to the struct in rax
	//| mov rax, rcx
	dasm_put(Dst, 246);

	size_t code_size;
	dasm_link(Dst, &code_size);
	void *mem = turkey_allocate_executable_memory(vm->tag, code_size);

	dasm_encode(Dst, mem);
	return mem;
}
