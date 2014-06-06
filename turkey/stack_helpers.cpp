#include "turkey_internal.h"
#include "hooks.h"

void turkey_push_string(TurkeyVM *vm, const char *string) {
	// find the length of the string
	const char *s = string;
	while(*s != '\0') {
		s++;
	}

	turkey_push_string_l(vm, string, (size_t)s - (size_t)string);
}

void turkey_push_string_l(TurkeyVM *vm, const char *string, unsigned int length) {
	TurkeyVariable var;
	var.type = TT_String;
	var.string = turkey_stringtable_newstring(vm, string, length);
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push_object(TurkeyVM *vm) {
	TurkeyVariable var;
	var.type = TT_Object;
	var.object = turkey_object_new(vm);
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push_buffer(TurkeyVM *vm, size_t size) {
	TurkeyVariable var;
	var.type = TT_Buffer;
	var.buffer = turkey_buffer_new(vm, size);
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push_buffer_wrapper(TurkeyVM *vm, size_t size, void *c) {
	TurkeyVariable var;
	var.type = TT_Buffer;
	var.buffer = turkey_buffer_new_native(vm, c, size);
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push_array(TurkeyVM *vm, size_t size) {
	TurkeyVariable var;
	var.type = TT_Array;
	var.array = turkey_array_new(vm, size);
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push_native_function(TurkeyVM *vm, TurkeyNativeFunction func, void *closure) {
	TurkeyVariable var;
	var.type = TT_FunctionPointer;
	var.function = turkey_functionpointer_new_native(vm, func, closure);
	turkey_stack_push(vm->local_stack, var);
}


void turkey_push_boolean(TurkeyVM *vm, bool val) {
	TurkeyVariable var;
	var.type = TT_Boolean;
	var.boolean_value = val;
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push_signed_integer(TurkeyVM *vm, signed long long int val) {
	TurkeyVariable var;
	var.type = TT_Signed;
	var.signed_value = val;
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push_unsigned_integer(TurkeyVM *vm, unsigned long long int val) {
	TurkeyVariable var;
	var.type = TT_Unsigned;
	var.unsigned_value = val;
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push_float(TurkeyVM *vm, double val) {
	TurkeyVariable var;
	var.type = TT_Float;
	var.float_value = val;
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push_null(TurkeyVM *vm) {
	TurkeyVariable var;
	var.type = TT_Null;
	turkey_stack_push(vm->local_stack, var);
}

void turkey_push(TurkeyVM *vm, TurkeyVariable &variable) {
	turkey_stack_push(vm->local_stack, variable);
}

void turkey_grab(TurkeyVM *vm, unsigned int index) {
	TurkeyVariable var;
	turkey_stack_get(vm->local_stack, index, var);
	turkey_stack_push(vm->local_stack, var);
}

TurkeyVariable turkey_pop(TurkeyVM *vm) {
	TurkeyVariable var;
	turkey_stack_pop(vm->local_stack, var);
	return var;
}

void turkey_pop_no_return(TurkeyVM *vm) {
	turkey_stack_pop_no_return(vm->local_stack);
}

void turkey_swap(TurkeyVM *vm, unsigned int ind1, unsigned int ind2) {
	TurkeyVariable a, b;
	turkey_stack_get(vm->local_stack, ind1, a);
	turkey_stack_get(vm->local_stack, ind2, b);

	turkey_stack_set(vm->local_stack, ind1, b);
	turkey_stack_set(vm->local_stack, ind2, a);
}

TurkeyVariable turkey_get(TurkeyVM *vm, unsigned int index) {
	TurkeyVariable var;
	turkey_stack_get(vm->local_stack, index, var);
	return var;
}

void turkey_set(TurkeyVM *vm, unsigned int index, TurkeyVariable &var) {
	turkey_stack_set(vm->local_stack, index, var);
}

/* objects and arrays */
TurkeyVariable turkey_get_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key) {
	TurkeyVariable obj;
	turkey_stack_get(vm->local_stack, ind_obj, obj);

	if(obj.type == TT_Object) {
		TurkeyVariable key;
		turkey_stack_get(vm->local_stack, ind_key, key);

		TurkeyString *str = turkey_to_string(vm, key);
		return turkey_object_get_property(vm, obj.object, str);
	} else if(obj.type == TT_Array) {
		TurkeyVariable key;
		turkey_stack_get(vm->local_stack, ind_key, key);

		double k = turkey_to_unsigned(vm, key);

		return turkey_array_get_element(vm, obj.array, k);
	} else {
		TurkeyVariable var;
		var.type = TT_Null;
		return var;
	}
}

void turkey_set_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key, unsigned int ind_val) {
	TurkeyVariable obj;
	turkey_stack_get(vm->local_stack, ind_obj, obj);

	if(obj.type == TT_Object) {
		TurkeyVariable key;
		turkey_stack_get(vm->local_stack, ind_key, key);

		TurkeyString *str = turkey_to_string(vm, key);
		turkey_gc_hold(vm, str, TT_String); // hold the string so to avoid any change of getting GC'd while we're setting the property
		
		TurkeyVariable val;
		turkey_stack_get(vm->local_stack, ind_val, val);

		turkey_object_set_property(vm, obj.object, str, val);
		turkey_gc_unhold(vm, str, TT_String);
	} else if(obj.type == TT_Array) {
		TurkeyVariable key;
		turkey_stack_get(vm->local_stack, ind_key, key);

		double k = turkey_to_unsigned(vm, key);

		TurkeyVariable val;
		turkey_stack_get(vm->local_stack, ind_val, val);

		turkey_array_set_element(vm, obj.array, k, val);
	}
}

void turkey_delete_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key) {
	TurkeyVariable obj;
	turkey_stack_get(vm->local_stack, ind_obj, obj);

	if(obj.type == TT_Object) {
		TurkeyVariable key;
		turkey_stack_get(vm->local_stack, ind_key, key);

		TurkeyString *str = turkey_to_string(vm, key);
		turkey_object_delete_property(vm, obj.object, str);
	} else if(obj.type == TT_Array) {
		TurkeyVariable key;
		turkey_stack_get(vm->local_stack, ind_key, key);

		double k = turkey_to_unsigned(vm, key);

		TurkeyVariable var;
		var.type = TT_Null;

		turkey_array_set_element(vm, obj.array, ind_key, var);
	}
}
