#include "turkey_internal.h"
#include "hooks.h"

TurkeyVM *turkey_init(TurkeySettings *settings) {
	TurkeyVM *vm = (TurkeyVM *)turkey_allocate_memory(sizeof TurkeyVM);
	vm->debug = settings->debug;

	vm->current_byteindex = 0;
	vm->current_function_index = 0;

	turkey_module_init(vm);
	turkey_stack_init(vm->local_stack);
	turkey_stack_init(vm->variable_stack);
	turkey_callstack_init(vm->call_stack);

	turkey_stringtable_init(vm->string_table);

	return vm;
};

void turkey_cleanup(TurkeyVM *vm) {
	turkey_stringtable_cleanup(vm->string_table);
	turkey_stack_cleanup(vm->local_stack);
	turkey_stack_cleanup(vm->variable_stack);
	turkey_callstack_cleanup(vm->call_stack);

	turkey_module_cleanup(vm);
	
	turkey_free_memory(vm);
};
