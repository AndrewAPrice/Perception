#include "turkey_internal.h"
#include "hooks.h"

TurkeyClosure *turkey_closure_create(TurkeyVM *vm, TurkeyClosure *parent, unsigned int variables) {
	TurkeyClosure *closure = (TurkeyClosure *)turkey_allocate_memory(sizeof TurkeyClosure);
	closure->references = 1; /* whoever created it has the first reference to it */
	closure->count = variables;
	closure->parent = parent;
	closure->variables = (TurkeyVariable *)turkey_allocate_memory((sizeof TurkeyVariable) * variables);

	return closure;
}

void turkey_closure_delete(TurkeyVM *vm, TurkeyClosure *closure) {
	assert(closure->references == 0);

	turkey_free_memory(closure->variables);

	/* dereference parent */
	if(closure->parent != 0) {
		closure->parent->references--;
		if(closure->parent->references == 0)
			turkey_closure_delete(vm, closure->parent);
	}

	turkey_free_memory(closure);
}