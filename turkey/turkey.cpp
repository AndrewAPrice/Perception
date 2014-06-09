#include "turkey_internal.h"
#include "hooks.h"

TurkeyVM *turkey_init(TurkeySettings *settings) {
	TurkeyVM *vm = (TurkeyVM *)turkey_allocate_memory(sizeof TurkeyVM);
	vm->debug = settings->debug;

	turkey_interpreter_init(vm);
	turkey_module_init(vm);
	turkey_stack_init(vm->local_stack);
	turkey_stack_init(vm->variable_stack);
	turkey_stack_init(vm->parameter_stack);

	turkey_stringtable_init(vm);

	return vm;
};

void turkey_cleanup(TurkeyVM *vm) {
	turkey_stringtable_cleanup(vm);
	turkey_stack_cleanup(vm->local_stack);
	turkey_stack_cleanup(vm->variable_stack);
	turkey_stack_cleanup(vm->parameter_stack);

	turkey_module_cleanup(vm);

	turkey_interpreter_cleanup(vm);
	
	turkey_free_memory(vm);
};
