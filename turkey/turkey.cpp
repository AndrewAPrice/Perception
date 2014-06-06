#include "turkey_internal.h"
#include "hooks.h"

TurkeyVM *turkey_init(TurkeySettings *settings) {
	TurkeyVM *vm = (TurkeyVM *)turkey_allocate_memory(sizeof TurkeyVM);
	vm->debug = settings->debug;

	vm->current_byteindex = 0;
	vm->current_function_index = 0;

	turkey_stack_init(vm->local_stack);
	turkey_stack_init(vm->variable_stack);
	turkey_callstack_init(vm->call_stack);

	vm->function_array.count = 0;
	vm->module_array.count = 0;

	turkey_stringtable_init(vm->string_table);

	return vm;
};

void turkey_cleanup(TurkeyVM *vm) {
	turkey_stringtable_cleanup(vm->string_table);
	turkey_stack_cleanup(vm->local_stack);
	turkey_stack_cleanup(vm->variable_stack);
	turkey_callstack_cleanup(vm->call_stack);

	if(vm->function_array.count > 0)
		turkey_free_memory(vm->function_array.functions);

	if(vm->module_array.count > 0)
		turkey_free_memory(vm->module_array.modules);

	turkey_free_memory(vm);
};
