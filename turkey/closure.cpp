#include "turkey_internal.h"
#include "hooks.h"

TurkeyClosure *turkey_closure_create(TurkeyVM *vm, TurkeyClosure *parent, unsigned int variables) {
	TurkeyClosure *closure = (TurkeyClosure *)turkey_allocate_memory(sizeof TurkeyClosure);
	closure->count = variables;
	closure->parent = parent;
	closure->variables = (TurkeyVariable *)turkey_allocate_memory((sizeof TurkeyVariable) * variables);
	for(unsigned int i = 0; i < variables; i++)
		closure->variables[i].type = TT_Null;

	/* register with the gc */
	turkey_gc_register_closure(vm->garbage_collector, closure);

	return closure;
}

void turkey_closure_delete(TurkeyVM *vm, TurkeyClosure *closure) {
	turkey_free_memory(closure->variables);
	turkey_free_memory(closure);
}

void turkey_closure_get(TurkeyVM *vm, unsigned int position, TurkeyVariable &value) {
	TurkeyClosure *closure = vm->interpreter_state->closure;

	while(closure != 0) {
		if(position < closure->count) {
			value = closure->variables[position];
			return;
		}

		position -= closure->count;
		closure = closure->parent;
	}

	// not found
	value.type = TT_Null;
}

void turkey_closure_set(TurkeyVM *vm, unsigned int position, const TurkeyVariable &value) {
	TurkeyClosure *closure = vm->interpreter_state->closure;

	while(closure != 0) {
		if(position < closure->count) {
			closure->variables[position] = value;
			return;
		}

		position -= closure->count;
		closure = closure->parent;
	}
}
