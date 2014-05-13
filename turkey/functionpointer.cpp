#include "turkey_internal.h"
#include "hooks.h"

TurkeyFunctionPointer *turkey_functionpointer_new(TurkeyVM *vm, TurkeyFunction *function, TurkeyClosure *closure) {
	closure->references++;

	TurkeyFunctionPointer *funcptr = (TurkeyFunctionPointer *)turkey_allocate_memory(sizeof TurkeyFunctionPointer);
	funcptr->function = function;
	funcptr->closure = closure;

	return funcptr;
}

void turkey_functionpointer_delete(TurkeyVM *vm, TurkeyFunctionPointer *funcptr) {
	/* dereference the closure */
	funcptr->closure->references--;
	if(funcptr->closure->references == 0)
		turkey_closure_delete(vm, funcptr->closure);

	turkey_free_memory(funcptr);
}