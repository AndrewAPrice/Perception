#ifdef TEST
#include "../turkey.h"
#include <stdio.h>
#include <time.h>
#include <Windows.h>
#include <stdarg.h>

LARGE_INTEGER startTime;
double counterFrequency;

#define TURKEY_ARG_START() unsigned int __turkey_argc = 0; va_list __turkey_va_list; va_start(__turkey_va_list, types);
#define TURKEY_ARG_GET(variable) { variable.type = (TurkeyType)((types >> (__turkey_argc << 2)) & 15); variable.unsigned_value = va_arg(__turkey_va_list, uint64_t); }
#define TURKEY_ARG_END() va_end(__turkey_va_list);

TurkeyVariable test_end(TurkeyVM *vm, void *closure, unsigned int argc, uint64_t types, ...) {
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);
	
	TurkeyVariable var;
	TURKEY_ARG_START(); TURKEY_ARG_GET(var); TURKEY_ARG_END();

	uint64_t result = turkey_to_unsigned(vm, var);
	
	printf("Test end. It took: %f Calculation result: %i\n", (double)(endTime.QuadPart - startTime.QuadPart) / counterFrequency, result);

	TurkeyVariable ret;
	ret.type = TT_Null;
	return ret;
}

void test_end(unsigned int result) {
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);

	printf("Test end. It took: %f Calculation result: %i\n", (double)(endTime.QuadPart - startTime.QuadPart) / counterFrequency, result);
}

TurkeyVariable test_begin(TurkeyVM *vm, void *closur, unsigned int argc, uint64_t types, ...) {
	TurkeyVariable var;
	TURKEY_ARG_START(); TURKEY_ARG_GET(var); TURKEY_ARG_END();

	turkey_gc_collect(vm);
	if(var.type == TT_String)
		printf("Starting test: %.*s\n", var.string->length, var.string->string);
	else
		printf("Starting test.\n");
	TurkeyVariable ret;
	ret.type = TT_Null;
	QueryPerformanceCounter(&startTime);
	return ret;
}

void test_begin(char *name) {
	printf("Starting test: %s\n", name);
	QueryPerformanceCounter(&startTime);
}

/*
TurkeyVariable test_log(TurkeyVM *vm, void *closure, unsigned int argc) {
	for(int i = (int)argc - 1; i >= 0; i--) {
		TurkeyVariable var = turkey_get(vm, i);
		if(var.type == TT_String) {
			printf("%.*s\n", var.string->length, var.string->string);
		}
	}

	TurkeyVariable var;
	var.type = TT_Null;
	return var;
};*/

void tests();

void main() {
	/* performance counter for profiling */
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	counterFrequency = (double)freq.QuadPart;

	/* create the vm */
	TurkeySettings settings;
	settings.debug = true;
	settings.tag = 0;

	TurkeyVM *vm = turkey_init(&settings);
	vm->tag = vm; /* our tag will be our vm, but in real uses it may be a process structure */

	/* create our global test module */
	TurkeyString *str_test = turkey_stringtable_newstring(vm, "Test");
	turkey_gc_hold(vm, str_test, TT_String);
	TurkeyVariable obj_test(turkey_object_new(vm));
	turkey_register_module(vm, str_test, obj_test);
	turkey_gc_unhold(vm, str_test, TT_String);

	/* register our begin function */
	TurkeyString *str_begin = turkey_stringtable_newstring(vm, "begin");
	turkey_gc_hold(vm, str_begin, TT_String);
	TurkeyVariable func_test_begin(turkey_functionpointer_new_native(vm, test_begin, 0));
	turkey_object_set_property(vm, obj_test.object, str_begin, func_test_begin);
	turkey_gc_unhold(vm, str_begin, TT_String);

	/* registr our end function */
	TurkeyString *str_end = turkey_stringtable_newstring(vm, "end");
	turkey_gc_hold(vm, str_end, TT_String);
	TurkeyVariable func_test_end(turkey_functionpointer_new_native(vm, test_end, 0));
	turkey_object_set_property(vm, obj_test.object, str_end, func_test_end);
	turkey_gc_unhold(vm, str_end, TT_String);

	/* run ../../shovel/test.bin */
	TurkeyString *str_path = turkey_stringtable_newstring(vm, "./benchmarks.bin");
	turkey_require(vm, str_path); /* loaded test.bin and pops off test.bin's exports */

	tests();
	
	/* cleanup the vm */
	turkey_cleanup(vm);
};

//////////////////// TEST CODE:
unsigned int fib(unsigned int n) {
	if(n <= 1)
		return n;
	return fib(n - 1) + fib(n - 2);
}

void fibonacci_test() {
	test_begin("Recursive Fibonacci");
	unsigned int result = fib(35);
	test_end(result);
};

void tests() {
	printf("Running native tests\n");
	fibonacci_test();
}

#endif
