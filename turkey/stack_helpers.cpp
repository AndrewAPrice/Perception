#include "turkey.h"
#include "hooks.h"

void turkey_push_string(TurkeyVM *vm, const char *string) {
	// find the length of the string
	const char *s = string;
	while(*s != '\0') {
		s++;
	}

	turkey_push_string_l(vm, string, (unsigned int)((size_t)s - (size_t)string));
}

void turkey_push_string_l(TurkeyVM *vm, const char *string, unsigned int length) {
	TurkeyVariable var;
	var.type = TT_String;
	var.string = turkey_stringtable_newstring(vm, string, length);
	vm->variable_stack.Push(var);
}

void turkey_push_object(TurkeyVM *vm) {
	TurkeyVariable var;
	var.type = TT_Object;
	var.object = turkey_object_new(vm);
	vm->variable_stack.Push(var);
}

void turkey_push_buffer(TurkeyVM *vm, size_t size) {
	TurkeyVariable var;
	var.type = TT_Buffer;
	var.buffer = turkey_buffer_new(vm, size);
	vm->variable_stack.Push(var);
}

void turkey_push_buffer_wrapper(TurkeyVM *vm, size_t size, void *c) {
	TurkeyVariable var;
	var.type = TT_Buffer;
	var.buffer = turkey_buffer_new_native(vm, c, size);
	vm->variable_stack.Push(var);
}

void turkey_push_array(TurkeyVM *vm, size_t size) {
	TurkeyVariable var;
	var.type = TT_Array;
	var.array = turkey_array_new(vm, (unsigned int)size);
	vm->variable_stack.Push(var);
}

void turkey_push_native_function(TurkeyVM *vm, TurkeyNativeFunction func, void *closure) {
	TurkeyVariable var;
	var.type = TT_FunctionPointer;
	var.function = turkey_functionpointer_new_native(vm, func, closure);
	vm->variable_stack.Push(var);
}


void turkey_push_boolean(TurkeyVM *vm, bool val) {
	TurkeyVariable var;
	var.type = TT_Boolean;
	var.boolean_value = val;
	vm->variable_stack.Push(var);
}

void turkey_push_signed_integer(TurkeyVM *vm, signed long long int val) {
	TurkeyVariable var;
	var.type = TT_Signed;
	var.signed_value = val;
	vm->variable_stack.Push(var);
}

void turkey_push_unsigned_integer(TurkeyVM *vm, unsigned long long int val) {
	TurkeyVariable var;
	var.type = TT_Unsigned;
	var.unsigned_value = val;
	vm->variable_stack.Push(var);
}

void turkey_push_float(TurkeyVM *vm, double val) {
	TurkeyVariable var;
	var.type = TT_Float;
	var.float_value = val;
	vm->variable_stack.Push(var);
}

void turkey_push_null(TurkeyVM *vm) {
	TurkeyVariable var;
	var.type = TT_Null;
	vm->variable_stack.Push(var);
}

void turkey_push(TurkeyVM *vm, TurkeyVariable &variable) {
	vm->variable_stack.Push(variable);
}

void turkey_grab(TurkeyVM *vm, unsigned int index) {
	TurkeyVariable var;
	if(!vm->variable_stack.Get(index, var)) var.type = TT_Null;
	vm->variable_stack.Push(var);
}

TurkeyVariable turkey_pop(TurkeyVM *vm) {
	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	return var;
}

void turkey_pop_no_return(TurkeyVM *vm) {
	vm->variable_stack.PopNoReturn();
}

void turkey_swap(TurkeyVM *vm, unsigned int ind1, unsigned int ind2) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Get(ind1, a)) a.type = TT_Null;
	if(!vm->variable_stack.Get(ind2, b)) b.type = TT_Null;

	vm->variable_stack.Set(ind1, b);
	vm->variable_stack.Set(ind2, a);
}

TurkeyVariable turkey_get(TurkeyVM *vm, unsigned int index) {
	TurkeyVariable var;
	if(!vm->variable_stack.Get(index, var)) var.type = TT_Null;
	return var;
}

void turkey_set(TurkeyVM *vm, unsigned int index, TurkeyVariable &var) {
	vm->variable_stack.Set(index, var);
}

/* objects and arrays */
TurkeyVariable turkey_get_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key) {
	TurkeyVariable obj;
	if(!vm->variable_stack.Get(ind_obj, obj)) obj.type = TT_Null;

	if(obj.type == TT_Object) {
		TurkeyVariable key;
		if(!vm->variable_stack.Get(ind_key, key)) key.type = TT_Null;

		TurkeyString *str = turkey_to_string(vm, key);
		return turkey_object_get_property(vm, obj.object, str);
	} else if(obj.type == TT_Array) {
		TurkeyVariable key;
		if(!vm->variable_stack.Get(ind_key, key)) key.type = TT_Null;

		unsigned int k = (unsigned int)turkey_to_unsigned(vm, key);

		return turkey_array_get_element(vm, obj.array, k);
	} else {
		TurkeyVariable var;
		var.type = TT_Null;
		return var;
	}
}

void turkey_set_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key, unsigned int ind_val) {
	TurkeyVariable obj;
	if(!vm->variable_stack.Get(ind_obj, obj)) obj.type = TT_Null;

	if(obj.type == TT_Object) {
		TurkeyVariable key;
		if(!vm->variable_stack.Get(ind_key, key)) key.type = TT_Null;

		TurkeyString *str = turkey_to_string(vm, key);
		turkey_gc_hold(vm, str, TT_String); // hold the string so to avoid any change of getting GC'd while we're setting the property
		
		TurkeyVariable val;
		if(!vm->variable_stack.Get(ind_val, val)) val.type = TT_Null;

		turkey_object_set_property(vm, obj.object, str, val);
		turkey_gc_unhold(vm, str, TT_String);
	} else if(obj.type == TT_Array) {
		TurkeyVariable key;
		if(!vm->variable_stack.Get(ind_key, key)) key.type = TT_Null;

		unsigned int k = (unsigned int)turkey_to_unsigned(vm, key);

		TurkeyVariable val;
		if(!vm->variable_stack.Get(ind_val, val)) val.type = TT_Null;

		turkey_array_set_element(vm, obj.array, k, val);
	}
}

void turkey_delete_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key) {
	TurkeyVariable obj;
	if(!vm->variable_stack.Get(ind_obj, obj)) obj.type = TT_Null;

	if(obj.type == TT_Object) {
		TurkeyVariable key;
		if(!vm->variable_stack.Get(ind_key, key)) key.type = TT_Null;

		TurkeyString *str = turkey_to_string(vm, key);
		turkey_object_delete_property(vm, obj.object, str);
	} else if(obj.type == TT_Array) {
		TurkeyVariable key;
		if(!vm->variable_stack.Get(ind_key, key)) key.type = TT_Null;

		unsigned long long int k = turkey_to_unsigned(vm, key);

		TurkeyVariable var;
		var.type = TT_Null;

		turkey_array_set_element(vm, obj.array, ind_key, var);
	}
}
