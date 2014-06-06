#include "turkey_internal.h"
#include "hooks.h"

TurkeyFunctionPointer *turkey_functionpointer_new(TurkeyVM *vm, TurkeyFunction *function, TurkeyClosure *closure) {
	closure->references++;

	TurkeyFunctionPointer *funcptr = (TurkeyFunctionPointer *)turkey_allocate_memory(sizeof TurkeyFunctionPointer);
	funcptr->is_native = false;
	funcptr->managed.function = function;
	funcptr->managed.closure = closure;

	return funcptr;
}

TurkeyFunctionPointer *turkey_functionpointer_new_native(TurkeyVM *vm, TurkeyNativeFunction function, void *closure) {
	TurkeyFunctionPointer *funcptr = (TurkeyFunctionPointer *)turkey_allocate_memory(sizeof TurkeyFunctionPointer);
	funcptr->is_native = true;
	funcptr->native.function = function;
	funcptr->native.closure = closure;

	return funcptr;
}

void turkey_functionpointer_delete(TurkeyVM *vm, TurkeyFunctionPointer *funcptr) {
	if(!funcptr->is_native) {
		/* dereference the closure */
		funcptr->managed.closure->references--;
		if(funcptr->managed.closure->references == 0)
			turkey_closure_delete(vm, funcptr->managed.closure);
	}

	turkey_free_memory(funcptr);
}