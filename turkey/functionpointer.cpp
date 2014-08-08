#include "turkey.h"
#include "hooks.h"

TurkeyFunctionPointer *turkey_functionpointer_new(TurkeyVM *vm, TurkeyFunction *function, TurkeyClosure *closure) {
	TurkeyFunctionPointer *funcptr = (TurkeyFunctionPointer *)turkey_allocate_memory(vm->tag, sizeof(TurkeyFunctionPointer));
	funcptr->is_native = false;
	funcptr->data.managed.function = function;
	funcptr->data.managed.closure = closure;

	/* register with the gc */
	turkey_gc_register_function_pointer(vm->garbage_collector, funcptr);

	return funcptr;
}

TurkeyFunctionPointer *turkey_functionpointer_new_native(TurkeyVM *vm, TurkeyNativeFunction function, void *closure) {
	TurkeyFunctionPointer *funcptr = (TurkeyFunctionPointer *)turkey_allocate_memory(vm->tag, sizeof(TurkeyFunctionPointer));
	funcptr->is_native = true;
	funcptr->data.native.function = function;
	funcptr->data.native.closure = closure;
	
	/* register with the gc */
	turkey_gc_register_function_pointer(vm->garbage_collector, funcptr);

	return funcptr;
}

void turkey_functionpointer_delete(TurkeyVM *vm, TurkeyFunctionPointer *funcptr) {
	turkey_free_memory(vm->tag, funcptr, sizeof(TurkeyFunctionPointer));
}