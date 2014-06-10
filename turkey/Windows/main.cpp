#include "../turkey_internal.h"
#include <stdio.h>

TurkeyVariable console_log(TurkeyVM *vm, void *closure, unsigned int argc) {
	for(int i = (int)argc - 1; i >= 0; i--) {
		TurkeyVariable var = turkey_get(vm, i);
		if(var.type == TT_String) {
			printf("%.*s\n", var.string->length, var.string->string);
		}
	}

	TurkeyVariable var;
	var.type = TT_Null;
	return var;
};

void main() {
	/* create the vm */
	TurkeySettings settings;
	settings.debug = true;
	settings.tag = 0;

	TurkeyVM *vm = turkey_init(&settings);

	/* create our global console module */
	turkey_push_object(vm); /* console obj */
	turkey_push_string(vm, "console");
	turkey_register_module(vm, 0, 1);
	turkey_pop(vm); /* pops off console string*/

	/* register our log function */
	turkey_push_string(vm, "log");
	turkey_push_native_function(vm, console_log, 0);
	turkey_set_element(vm, 2, 1, 0);
	turkey_pop(vm); /* pops off function */
	turkey_pop(vm); /* pops off log string */
	turkey_pop(vm); /* pops off console object */

	/* run ../../shovel/test.bin */
	turkey_push_string(vm, "../../shovel/test.bin");
	turkey_require(vm); /* loaded test.bin and pops off test.bin's exports */
	turkey_pop(vm); /* pops off path */
	printf("Before: %i\n", vm->garbage_collector.items);
	turkey_gc_collect(vm);
	printf("After: %i\n", vm->garbage_collector.items);
	
	/* cleanup the vm */
	turkey_cleanup(vm);
};
