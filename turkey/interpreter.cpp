#include "hooks.h"
#include "turkey.h"

void turkey_interpreter_init(TurkeyVM *vm) {
	vm->interpreter_state = 0;
}

void turkey_interpreter_cleanup(TurkeyVM *vm) {
	assert(vm->interpreter_state == 0); // still in the middle of vm execution
}

TurkeyVariable turkey_call_function(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, unsigned int argc) {
	if(funcptr->is_native) {
		// native function call
		TurkeyVariable var = funcptr->native.function(vm, funcptr->native.closure, (unsigned int)argc);
		while(argc > 0) {
			vm->variable_stack.PopNoReturn();
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

	TurkeyVariable nullvar;
	nullvar.type = TT_Null;
	
	// do we have enough parameters on our stack?
	{
		unsigned int stack_size = vm->variable_stack.position - vm->variable_stack.top;
		while(stack_size < argc) {
			// not enough parameters on the local stack, push nulls
			vm->variable_stack.Push(nullvar);
			stack_size++;
		}
	}
	
	// set up local stack
	unsigned int caller_variable_stack_top = vm->variable_stack.top;
	vm->variable_stack.top = vm->variable_stack.position - argc;

	if(argc > func->parameters) {
		// too many parameters
		vm->variable_stack.position -= argc - func->parameters;
	} else {
		while(argc < func->parameters) {
			// too few parameters
			vm->variable_stack.Push(nullvar);
			argc++;
		}
	}
	
	// start executing
	while(state.executing && state.code_start <= state.code_ptr && state.code_ptr < state.code_end) {
		// call this instruction
		unsigned char bytecode = *(unsigned char *)state.code_ptr++;
		turkey_interpreter_operations[bytecode](vm);
	}

	// get the return value
	TurkeyVariable ret;
	if(!vm->variable_stack.Pop(ret)) ret.type = TT_Null;

	// return to the parent state
	vm->variable_stack.position = vm->variable_stack.top;
	vm->variable_stack.top = caller_variable_stack_top;

	vm->interpreter_state = state.parent;

	return ret;
}

void turkey_call_function_no_return(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, unsigned int argc) {
	turkey_call_function(vm, funcptr, argc);
}
