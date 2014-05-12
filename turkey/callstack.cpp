#include "turkey_internal.h"
#include "hooks.h"
#ifdef DEBUG
#include <assert.h>
#endif

void turkey_callstack_init(TurkeyCallStack &stack) {
	stack.current = 0;
	stack.length = 16; /* initial stack size */

	stack.entries = (TurkeyCallStackEntry *)turkey_allocate_memory((sizeof TurkeyCallStackEntry) * stack.length);
}

void turkey_callstack_cleanup(TurkeyCallStack &stack) {
	turkey_free_memory(stack.entries);
}

/* adds and returns the entry (to write into) */
TurkeyCallStackEntry *turkey_callstack_push(TurkeyCallStack &stack) {
	if(stack.length == stack.current) {
		/* not enough room to push a variable on */
		unsigned int newLength = stack.length * 2;

		stack.entries = (TurkeyCallStackEntry *)turkey_reallocate_memory(stack.entries, (sizeof TurkeyCallStackEntry) * newLength);
		stack.length = newLength;
	}

	TurkeyCallStackEntry *entry = &(stack.entries[stack.current]);
	
	stack.current++;
	return entry;
}

/* pops an element and returns the entry */
TurkeyCallStackEntry *turkey_callstack_pop(TurkeyCallStack &stack) {
#ifdef DEBUG
	assert(stack.current > 0);
#endif

	stack.current--;
	return &(stack.entries[stack.current]);
}
