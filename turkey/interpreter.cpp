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
		TurkeyVariable var = funcptr->native.function(vm, funcptr->native.closure, argc);
		while(argc > 0) {
			turkey_stack_pop_no_return(vm->variable_stack);
			argc--;
		}
		return var;
	}

	// call managed function
	TurkeyFunction *func = funcptr->managed.function;

	TurkeyInterpreterState state;
	state.parent = vm->interpreter_state;
	state.function = func;
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

	// copy parameters
	unsigned int callee_parameter_stack_top = vm->parameter_stack.top;
	vm->parameter_stack.top = vm->parameter_stack.position;

	for(size_t i = 0; i < argc; i++) {
		TurkeyVariable var;
		turkey_stack_pop(vm->variable_stack, var);
		turkey_stack_push(vm, vm->parameter_stack, var);
	}
	
	// create space for local stack
	unsigned int callee_local_stack_top = vm->local_stack.top;
	vm->local_stack.top = vm->local_stack.position;
	TurkeyVariable nullVar;
	nullVar.type = TT_Null;

	for(size_t i = 0; i < func->locals; i++)
		turkey_stack_push(vm, vm->local_stack, nullVar);
	
	// new variable stack
	unsigned int callee_variable_stack_top = vm->variable_stack.top;
	vm->variable_stack.top = vm->variable_stack.position;

	// start executing
	while(state.executing && state.code_start <= state.code_ptr && state.code_ptr < state.code_end) {
		// call this instruction
		unsigned char bytecode = *(unsigned char *)state.code_ptr++;
		turkey_interpreter_operations[bytecode](vm);
	}

	// get the return value
	TurkeyVariable ret;
	turkey_stack_pop(vm->variable_stack, ret);

	// return to the parent state
	vm->parameter_stack.position = vm->parameter_stack.top;
	vm->parameter_stack.top = callee_parameter_stack_top;

	vm->local_stack.position = vm->local_stack.top;
	vm->local_stack.top = callee_local_stack_top;

	vm->variable_stack.position = vm->variable_stack.top;
	vm->variable_stack.top = callee_variable_stack_top;

	vm->interpreter_state = state.parent;

	return ret;
}

void turkey_call_function_no_return(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, size_t argc) {
	turkey_call_function(vm, funcptr, argc);
}
