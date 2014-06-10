#include "turkey.h"
#include "hooks.h"

TurkeyVM *turkey_init(TurkeySettings *settings) {
	TurkeyVM *vm = (TurkeyVM *)turkey_allocate_memory(settings->tag, sizeof TurkeyVM);
	vm->debug = settings->debug;
	vm->tag = settings->tag;

	turkey_gc_init(vm);
	turkey_interpreter_init(vm);
	turkey_module_init(vm);
	turkey_stack_init(vm, vm->local_stack);
	turkey_stack_init(vm, vm->variable_stack);
	turkey_stack_init(vm, vm->parameter_stack);

	turkey_stringtable_init(vm);

	return vm;
};

void turkey_cleanup(TurkeyVM *vm) {
	turkey_module_cleanup(vm);
	turkey_stringtable_cleanup(vm);
	turkey_stack_cleanup(vm, vm->local_stack);
	turkey_stack_cleanup(vm, vm->variable_stack);
	turkey_stack_cleanup(vm, vm->parameter_stack);

	turkey_interpreter_cleanup(vm);
	turkey_gc_cleanup(vm);
	
	turkey_free_memory(vm->tag, vm, sizeof TurkeyVM);
};
