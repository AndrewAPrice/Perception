#include "hooks.h"
#include "turkey_internal.h"

void turkey_interpreter_init(TurkeyVM *vm) {
	vm->interpreter_state = 0;
}

void turkey_interpreter_cleanup(TurkeyVM *vm) {
	assert(vm->interpreter_state == 0); // still in the middle of vm execution
}

TurkeyVariable turkey_call_function(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, size_t argc) {
	if(funcptr->is_native) {
		// native function call
		return;
	}

	// call managed function
	TurkeyFunction *func = funcptr->managed.function;

	TurkeyInterpreterState state;
	state.parent = vm->interpreter_state;
	vm->interpreter_state = &state;

	// set up closures
	state.closure = funcptr->managed.closure;
	if(func->closures > 0) {
		state.closure = turkey_closure_create(vm, funcptr->managed.closure, func->closures);
	} else
		state.closure = funcptr->managed.closure;

	state.code_start = state.code_ptr = (size_t)func->start;
	state.code_end = (size_t)func->end;
	state.executing = true;

	// create space for local variables

	// copy parameters

	// start executing
	while(state.executing && state.code_end > state.code_ptr > state.code_start) {
		// call this instruction
		turkey_interpreter_operations[*(unsigned char *)state.code_ptr++](vm);
	}

	// return to the parent state
	vm->interpreter_state = state.parent;
}

void turkey_call_function_no_return(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, size_t argc) {
	turkey_call_function(vm, funcptr, argc);
}
