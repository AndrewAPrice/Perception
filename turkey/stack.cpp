#include "turkey.h"
#include "hooks.h"

void turkey_stack_init(TurkeyVM *vm, TurkeyStack &stack) {
	stack.top = 0;
	stack.position = 0;
	stack.length = 16; /* initial stack size */

	stack.variables = (TurkeyVariable *)turkey_allocate_memory(vm->tag, (sizeof TurkeyVariable) * stack.length);
}

void turkey_stack_cleanup(TurkeyVM *vm, TurkeyStack &stack) {
	turkey_free_memory(vm->tag, stack.variables, (sizeof TurkeyVariable) * stack.length);
}

void turkey_stack_push(TurkeyVM *vm, TurkeyStack &stack, const TurkeyVariable &value) {
	if(stack.length == stack.position) {
		/* not enough room to push a variable on */
		unsigned int newLength = stack.length * 2;

		stack.variables = (TurkeyVariable *)turkey_reallocate_memory(vm->tag, stack.variables,
			(sizeof TurkeyVariable) * stack.length, (sizeof TurkeyVariable) * newLength);
		stack.length = newLength;
	}
	
	stack.variables[stack.position] = value;
	stack.position++;
}

void turkey_stack_pop_no_return(TurkeyStack &stack) {
	if(stack.position > stack.top)
		stack.position--;
}

	void turkey_stack_pop(TurkeyStack &stack, TurkeyVariable &value) {
	if(stack.position == stack.top) {
		value.type = TT_Null;
		return;
	}

	stack.position--;
	value = stack.variables[stack.position];
}

/* 0 is the bottom of the stack, indices are backwards */
void turkey_stack_get(TurkeyStack &stack, unsigned int position, TurkeyVariable &value) {
	if(position >= (stack.position - stack.top)) {
		value.type = TT_Null;
		return;
	}

	value = stack.variables[stack.position - position - 1];
}

/* 0 is the bottom of the stack, indices are backwards */
void turkey_stack_set(TurkeyStack &stack, unsigned int position, const TurkeyVariable &value) {
	if(position >= (stack.position - stack.top))
		return;

	stack.variables[stack.position - position - 1] = value;
}