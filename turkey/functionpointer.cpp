#include "turkey_internal.h"
#include "hooks.h"

TurkeyFunctionPointer *turkey_functionpointer_new(TurkeyVM *vm, TurkeyFunction *function, TurkeyClosure *closure) {

	TurkeyFunctionPointer *funcptr = (TurkeyFunctionPointer *)turkey_allocate_memory(sizeof TurkeyFunctionPointer);
	funcptr->is_native = false;
	funcptr->managed.function = function;
	funcptr->managed.closure = closure;

	/* register with the gc */
	turkey_gc_register_function_pointer(vm->garbage_collector, funcptr);

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
	turkey_free_memory(funcptr);
}