#ifdef TEST
#include "../turkey.h"
#include <stdio.h>
#include <time.h>
#include <Windows.h>

LARGE_INTEGER startTime;
double counterFrequency;

TurkeyVariable test_end(TurkeyVM *vm, void *closure, unsigned int argc) {
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);

	TurkeyVariable var = turkey_get(vm, 0);
	unsigned long long int result = turkey_to_unsigned(vm, var);
	
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

TurkeyVariable test_begin(TurkeyVM *vm, void *closur, unsigned int argc) {
	TurkeyVariable var = turkey_get(vm, 0);
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
	turkey_push_object(vm); /* console obj */
	turkey_push_string(vm, "Test");
	turkey_register_module(vm, 0, 1);
	turkey_pop(vm); /* pops off console string*/

	/* register our begin function */
	turkey_push_string(vm, "begin");
	turkey_push_native_function(vm, test_begin, 0);
	turkey_set_element(vm, 2, 1, 0);
	turkey_pop(vm); /* pops off function */
	turkey_pop(vm); /* pops off begin string */

	/* registr our end function */
	turkey_push_string(vm, "end");
	turkey_push_native_function(vm, test_end, 0);
	turkey_set_element(vm, 2, 1, 0);
	turkey_pop(vm); /* pops off function */
	turkey_pop(vm); /* pops off end string */
	turkey_pop(vm); /* pops off console object */

	/* run ../../shovel/test.bin */
	turkey_push_string(vm, "./tests.bin");
	turkey_require(vm); /* loaded test.bin and pops off test.bin's exports */
	turkey_pop(vm); /* pops off path */

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
