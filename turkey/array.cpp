#include "turkey_internal.h"
#include "hooks.h"

TurkeyArray *turkey_array_new(TurkeyVM *vm, unsigned int size) {
	if(size == 0)
		size = 1;

	TurkeyArray *arr = (TurkeyArray *)turkey_allocate_memory(sizeof TurkeyArray);
	arr->allocated = size;
	arr->length = size;
	arr->elements = (TurkeyVariable *)turkey_allocate_memory((sizeof TurkeyVariable) * size);

	for(unsigned int i = 0; i < size; i++)
		arr->elements[i].type = TT_Null;

	/* register with the gc */
	turkey_gc_register_array(vm->garbage_collector, arr);
	return arr;
}

TurkeyArray *turkey_array_append(TurkeyVM *vm, TurkeyArray *a, TurkeyArray *b) {
	turkey_gc_hold(vm, a, TT_Array);
	turkey_gc_hold(vm, b, TT_Array);

	unsigned int length = a->allocated + b->allocated;
	if(length == 0)
		length = 1;

	unsigned int size = length - 1;
	size |= size >> 1;
	size |= size >> 2;
	size |= size >> 4;
	size |= size >> 8;
	size |= size >> 16;
	size++;

	TurkeyArray *arr = (TurkeyArray *)turkey_allocate_memory(sizeof TurkeyArray);
	arr->allocated = length;
	arr->length = size;
	arr->elements = (TurkeyVariable *)turkey_allocate_memory((sizeof TurkeyVariable) * size);

	for(unsigned int i = 0; i < a->allocated && i < size; i++)
		arr->elements[i] = a->elements[i];

	for(unsigned int i = a->allocated; i < a->allocated + b->allocated && i < size; i++)
		arr->elements[i] = b->elements[i - a->allocated];

	for(unsigned int i = a->allocated + b->allocated; i < size; i++)
		arr->elements[i].type = TT_Null;

	turkey_gc_unhold(vm, a, TT_Array);
	turkey_gc_unhold(vm, b, TT_Array);
	
	/* register with the gc */
	turkey_gc_register_array(vm->garbage_collector, arr);
	return arr;
}

/* release the memory used by the array, called from the garbage collector */
void turkey_array_delete(TurkeyVM *vm, TurkeyArray *arr) {
	turkey_free_memory(arr->elements);
	turkey_free_memory(arr);
}

void turkey_array_push(TurkeyVM *vm, TurkeyArray *arr, const TurkeyVariable &variable) {
	/* do we need to allocate more memory? */
	if(arr->length == arr->allocated)
		turkey_array_grow(vm, arr);

	arr->elements[arr->length] = variable;
	arr->length++;
}

/* double the size of the array */
void turkey_array_grow(TurkeyVM *vm, TurkeyArray *arr) {
	/* round to an upper power of two */
	unsigned int new_size = arr->allocated - 1;
	new_size |= new_size >> 1;
	new_size |= new_size >> 2;
	new_size |= new_size >> 4;
	new_size |= new_size >> 8;
	new_size |= new_size >> 16;
	new_size++;

	/* double it if it's already a power of two */
	if(new_size == arr->allocated)
		new_size *= 2;

	arr->elements = (TurkeyVariable *)turkey_reallocate_memory(arr->elements, (sizeof TurkeyVariable) * new_size);

	/* initialize the new elements to null */
	for(unsigned int i = arr->allocated; i < new_size; i++)
		arr->elements[i].type = TT_Null;

	arr->allocated = new_size;
}

void turkey_array_allocate(TurkeyVM *vm, TurkeyArray *arr, unsigned int size) {
	/* resize the allocated amount, but don't change the number of elements in the array */
	if(size == 0)
		size = 1;

	arr->elements = (TurkeyVariable *)turkey_reallocate_memory(arr->elements, (sizeof TurkeyVariable) * size);
	
	/* initialize the new elements to null */
	for(unsigned int i = arr->allocated; i < size; i++)
		arr->elements[i].type = TT_Null;
	
	arr->allocated = size;

	/* prune the length if we no longer have this allocated */
	if(arr->length > arr->allocated)
		arr->length = arr->allocated;
}

void turkey_array_resize(TurkeyVM *vm, TurkeyArray *arr, unsigned int size) {
	/* resize the allocated amount and change the number of elements in the array */
	if(size == 0)
		size = 1;
		
	/* initialize the new elements to null */
	for(unsigned int i = arr->allocated; i < size; i++)
		arr->elements[i].type = TT_Null;

	arr->allocated = size;
	arr->length = size;
}

TurkeyVariable turkey_array_get_element(TurkeyVM *vm, TurkeyArray *arr, unsigned int index) {
	if(index >= arr->length) {
		TurkeyVariable var;
		var.type = TT_Null;
		return var;
	}

	return arr->elements[index];
}

void turkey_array_set_element(TurkeyVM *vm, TurkeyArray *arr, unsigned int index, const TurkeyVariable &variable) {
	/* do we need to allocate more memory? */
	if(index > arr->allocated)
		turkey_array_resize(vm, arr, index + 1);

	if(index >= arr->length)
		arr->length = index + 1;

	arr->elements[index] = variable;
}

void turkey_array_remove(TurkeyVM *vm, TurkeyArray *arr, unsigned int start, unsigned int count) {
	unsigned int end = start + count;
	if(end > arr->length)
		end = arr->length;

	if(start > arr->length)
		start = arr->length;

	/* move everything above this down */
	unsigned int to_remove = end - start;

	unsigned int new_length = arr->length - to_remove;

	/* shift above elements down */
	for(unsigned int i = end; i < new_length; i++)
		arr->elements[i] = arr->elements[i + to_remove];

	/* set the elements afer those we've shifted down to null */
	for(unsigned int i = new_length; i < arr->length; i++)
		arr->elements[i].type = TT_Null;

	arr->length = new_length;
}

TurkeyArray *turkey_array_splice(TurkeyVM *vm, TurkeyArray *arr, unsigned int start, unsigned int count) {
	unsigned int end = start + count;
	if(end > arr->length)
		end = arr->length;

	if(start > arr->length)
		start = arr->length;

	count = end-start;

	/* create a new array */
	TurkeyArray *new_arr = turkey_array_new(vm, count);

	/* copy those elements into it */
	for(unsigned int i = start, j = 0; i < end; i++, j++)
		new_arr->elements[j] = arr->elements[i];

	return new_arr;
}

void turkey_array_insert(TurkeyVM *vm, TurkeyArray *arr, unsigned int index, const TurkeyVariable &variable) {
	/* if we're inserting beyond (not in the array or at the end of the array), then it's just a set_element logic rather than an insert */
	if(index > arr->length) {
		turkey_array_set_element(vm, arr, index, variable);
		return;
	}

	/* do we need to allocate more memory? */
	if(arr->length == arr->allocated)
		turkey_array_grow(vm, arr);

	/* shift everything up */
	for(unsigned i = arr->length; i > index; i--)
		arr->elements[i] = arr->elements[i - 1];

	arr->elements[index] = variable;
	arr->length++;
}