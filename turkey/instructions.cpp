#include "hooks.h"
#include "turkey.h"

// do nothing
void turkey_interpreter_instruction_nop(TurkeyVM *vm) {
}

void turkey_interpreter_instruction_add(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) b.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	case TT_Array:
		if(b.type != TT_Array)
			ret.type = TT_Null;
		else
			ret.array = turkey_array_append(vm, a.array, b.array);
		break;
	case TT_Boolean:
		ret.boolean_value = a.boolean_value || turkey_to_boolean(vm, b);
		break;
	case TT_Buffer:
		if(b.type != TT_Buffer)
			ret.type = TT_Null;
		else
			ret.buffer = turkey_buffer_append(vm, a.buffer, b.buffer);
		break;
	case TT_Float:
		ret.float_value = a.float_value + turkey_to_float(vm, b);
		break;
	default:
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value + turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_add, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value + turkey_to_signed(vm, b);
		break;
	case TT_String: {
		turkey_gc_hold(vm, a.string, TT_String);
		ret.string = turkey_string_append(vm, a.string, turkey_to_string(vm, b));
		turkey_gc_unhold(vm, a.string, TT_String);
		break; }
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_subtract(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Boolean:
		ret.boolean_value = a.boolean_value && turkey_to_boolean(vm, b);
		break;
	case TT_Float:
		ret.float_value = a.float_value - turkey_to_float(vm, b);
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value - turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_subtract, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value - turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_divide(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
		ret.float_value = a.float_value / turkey_to_float(vm, b);
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value / turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_divide, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value / turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_multiply(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
		ret.float_value = a.float_value * turkey_to_float(vm, b);
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value * turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_multiply, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value * turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_modulo(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	case TT_Float:
		ret.float_value = turkey_float_modulo(a.float_value, turkey_to_float(vm, b));
		break;
	default:
		ret.type = TT_Null;
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value % turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_modulo, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value % turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_increment(TurkeyVM *vm) {
	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Boolean:
		ret.boolean_value = true;
		break;
	case TT_Float:
		ret.float_value = a.float_value + 1.0;
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value + 1;
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_increment);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value + 1;
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_decrement(TurkeyVM *vm) {
	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Boolean:
		ret.boolean_value = false;
		break;
	case TT_Float:
		ret.float_value = a.float_value - 1.0;
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value - 1;
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_decrement);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value - 1;
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_xor(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
		ret.type = TT_Signed;
		ret.signed_value = (signed long long int)a.float_value ^ turkey_to_signed(vm, b);
		break;
	case TT_Boolean:
		ret.boolean_value = a.boolean_value ^ turkey_to_boolean(vm, b);
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value ^ turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_xor, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value ^ turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_and(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
		ret.type = TT_Signed;
		ret.signed_value = (signed long long int)a.float_value & turkey_to_signed(vm, b);
		break;
	case TT_Boolean:
		ret.boolean_value = a.boolean_value & turkey_to_boolean(vm, b);
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value & turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_and, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value & turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_or(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
		ret.type = TT_Signed;
		ret.signed_value = (signed long long int)a.float_value | turkey_to_signed(vm, b);
		break;
	case TT_Boolean:
		ret.boolean_value = a.boolean_value | turkey_to_boolean(vm, b);
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value | turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_or, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value | turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_not(TurkeyVM *vm) {
	
	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
		ret.type = TT_Signed;
		ret.signed_value = ~(signed long long int)a.float_value;
		break;
	case TT_Boolean:
		ret.boolean_value = !a.boolean_value;
		break;
	case TT_Unsigned:
		ret.unsigned_value = ~a.unsigned_value;
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_not);
		return;
	case TT_Signed:
		ret.signed_value = ~a.signed_value;
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_shift_left(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
	case TT_Boolean:
		ret.type = TT_Signed;
		ret.signed_value = turkey_to_signed(vm, a) << turkey_to_signed(vm, b);
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value << turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_shift_left, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value << turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_shift_right(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
	case TT_Boolean:
		ret.type = TT_Signed;
		ret.signed_value = turkey_to_signed(vm, a) >> turkey_to_signed(vm, b);
		break;
	case TT_Unsigned:
		ret.unsigned_value = a.unsigned_value >> turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_shift_right, b);
		return;
	case TT_Signed:
		ret.signed_value = a.signed_value >> turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_rotate_left(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
	case TT_Boolean:
	case TT_Unsigned:
	case TT_Signed:{
		ret.type = TT_Unsigned;
		unsigned long long int _a = turkey_to_signed(vm, a);
		unsigned long long int _b = turkey_to_signed(vm, b);
		ret.unsigned_value = (_a << _b) | (_a >> (64 - _b));
		break; }
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_rotate_left, b);
		return;
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_rotate_right(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Float:
	case TT_Boolean:
	case TT_Unsigned:
	case TT_Signed:{
		ret.type = TT_Unsigned;
		unsigned long long int _a = turkey_to_signed(vm, a);
		unsigned long long int _b = turkey_to_signed(vm, b);
		ret.unsigned_value = (_a >> _b) | (_a << (64 - _b));
		break; }
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_rotate_right, b);
		return;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_is_null(TurkeyVM *vm) {
	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;

	var.boolean_value = var.type == TT_Null;
	var.type = TT_Boolean;

	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_is_not_null(TurkeyVM *vm) {
	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;

	var.boolean_value = var.type != TT_Null;
	var.type = TT_Boolean;

	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_equals(TurkeyVM *vm) {
	TurkeyVariable var1, var2, out;
	if(!vm->variable_stack.Pop(var1)) var1.type = TT_Null;
	if(!vm->variable_stack.Pop(var2)) var2.type = TT_Null;

	if(var1.type != var2.type) {
		if(TURKEY_IS_TYPE_NUMBER(var1.type) && TURKEY_IS_TYPE_NUMBER(var2.type)) {
			if(var1.type == TT_Float || var2.type == TT_Float)
				out.boolean_value = turkey_to_float(vm, var1) == turkey_to_float(vm, var2);
			else if(var1.type == TT_Signed || var2.type == TT_Signed)
				out.boolean_value = turkey_to_signed(vm, var1) ==  turkey_to_signed(vm, var2);
			else if(var1.type == TT_Unsigned || var2.type == TT_Unsigned)
				out.boolean_value = turkey_to_unsigned(vm, var1) == turkey_to_unsigned(vm, var2);
			else
				out.boolean_value = turkey_to_boolean(vm, var1) == turkey_to_boolean(vm, var2);
		} else
			out.boolean_value = false;
	} else {
		if(var1.type != TT_Null) // all null = null
			out.boolean_value = true;
		else
			out.boolean_value = var1.unsigned_value == var2.unsigned_value;
	}

	out.type = TT_Boolean;

	vm->variable_stack.Push(out);
}

void turkey_interpreter_instruction_not_equals(TurkeyVM *vm) {
	TurkeyVariable var1, var2, out;
	if(!vm->variable_stack.Pop(var1)) var1.type = TT_Null;
	if(!vm->variable_stack.Pop(var2)) var2.type = TT_Null;

	if(var1.type != var2.type) {
		if(TURKEY_IS_TYPE_NUMBER(var1.type) && TURKEY_IS_TYPE_NUMBER(var2.type)) {
			if(var1.type == TT_Float || var2.type == TT_Float)
				out.boolean_value = turkey_to_float(vm, var1) != turkey_to_float(vm, var2);
			else if(var1.type == TT_Signed || var2.type == TT_Signed)
				out.boolean_value = turkey_to_signed(vm, var1) !=  turkey_to_signed(vm, var2);
			else if(var1.type == TT_Unsigned || var2.type == TT_Unsigned)
				out.boolean_value = turkey_to_unsigned(vm, var1) != turkey_to_unsigned(vm, var2);
			else
				out.boolean_value = turkey_to_boolean(vm, var1) != turkey_to_boolean(vm, var2);
		} else
			out.boolean_value = true;
	} else {
		if(var1.type != TT_Null) // all null = null
			out.boolean_value = false;
		else
			out.boolean_value = var1.unsigned_value != var2.unsigned_value;
	}

	out.type = TT_Boolean;
	vm->variable_stack.Push(out);
}

void turkey_interpreter_instruction_less_than(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = TT_Boolean;
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Boolean:
		ret.boolean_value = a.boolean_value && !(turkey_to_boolean(vm, b));
		break;
	case TT_Float:
		ret.boolean_value = a.float_value < turkey_to_float(vm, b);
		break;
	case TT_Unsigned:
		ret.boolean_value = a.unsigned_value < turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_less_than, b);
		return;
	case TT_Signed:
		ret.boolean_value = a.signed_value < turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_greater_than(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = TT_Boolean;
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Boolean:
		ret.boolean_value = !a.boolean_value && (turkey_to_boolean(vm, b));
		break;
	case TT_Float:
		ret.boolean_value = a.float_value > turkey_to_float(vm, b);
		break;
	case TT_Unsigned:
		ret.boolean_value = a.unsigned_value > turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_greater_than, b);
		return;
	case TT_Signed:
		ret.boolean_value = a.signed_value > turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_less_than_or_equals(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = TT_Boolean;
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Boolean:
		ret.boolean_value = !a.boolean_value;
		break;
	case TT_Float:
		ret.boolean_value = a.float_value <= turkey_to_float(vm, b);
		break;
	case TT_Unsigned:
		ret.boolean_value = a.unsigned_value <= turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_less_than_or_equals, b);
		return;
	case TT_Signed:
		ret.boolean_value = a.signed_value <= turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_greater_than_or_equals(TurkeyVM *vm) {
	TurkeyVariable a, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = TT_Boolean;
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Boolean:
		ret.boolean_value = !turkey_to_boolean(vm, b);
		break;
	case TT_Float:
		ret.boolean_value = a.float_value >= turkey_to_float(vm, b);
		break;
	case TT_Unsigned:
		ret.boolean_value = a.unsigned_value >= turkey_to_unsigned(vm, b);
		break;
	case TT_Object:
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_greater_than_or_equals, b);
		return;
	case TT_Signed:
		ret.boolean_value = a.signed_value >= turkey_to_signed(vm, b);
		break;
	}

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_is_true(TurkeyVM *vm) {
	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	bool _a = turkey_to_boolean(vm, a);

	TurkeyVariable ret;
	ret.type = TT_Boolean;
	ret.boolean_value = _a;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_is_false(TurkeyVM *vm) {
	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	bool _a = turkey_to_boolean(vm, a);

	TurkeyVariable ret;
	ret.type = TT_Boolean;
	ret.boolean_value = !_a;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_pop(TurkeyVM *vm) {
	vm->variable_stack.PopNoReturn();
}

void turkey_interpreter_instruction_pop_many(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	size_t count = (size_t)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	while(count > 0) {
		vm->variable_stack.PopNoReturn();
		count--;
	}
}

void turkey_interpreter_instruction_grab_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned int grab = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;
	
	turkey_grab(vm, grab);
}

void turkey_interpreter_instruction_grab_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	unsigned int grab = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;
	
	turkey_grab(vm, grab);
}

void turkey_interpreter_instruction_grab_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned int grab = *(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;
	
	turkey_grab(vm, grab);
}



void turkey_interpreter_instruction_push_many_nulls(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	size_t nulls = (size_t)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	for(;nulls > 0; nulls--)
		turkey_push_null(vm);
}

/*
void turkey_interpreter_instruction_load_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	TurkeyVariable var;
	turkey_stack_get(vm->local_stack, a, var);
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_load_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;

	TurkeyVariable var;
	turkey_stack_get(vm->local_stack, a, var);
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_load_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = *(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;

	TurkeyVariable var;
	turkey_stack_get(vm->local_stack, a, var);
	vm->variable_stack.Push(var);
}*/

void turkey_interpreter_instruction_store_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	vm->variable_stack.Set(a, var);
}

void turkey_interpreter_instruction_store_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;

	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	vm->variable_stack.Set( a, var);
}

void turkey_interpreter_instruction_store_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = *(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;

	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	vm->variable_stack.Set( a, var);
}

void turkey_interpreter_instruction_swap_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;
	unsigned int b = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;
	
	turkey_swap(vm, a, b);
}

void turkey_interpreter_instruction_swap_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;
	unsigned int b = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;
	
	turkey_swap(vm, a, b);
}

void turkey_interpreter_instruction_swap_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 7 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;
	unsigned int b = (unsigned int)*(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;
	
	turkey_swap(vm, a, b);
}

void turkey_interpreter_instruction_load_closure_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	TurkeyVariable var;
	turkey_closure_get(vm, a, var);
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_load_closure_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;

	TurkeyVariable var;
	turkey_closure_get(vm, a, var);
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_load_closure_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = *(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;

	TurkeyVariable var;
	turkey_closure_get(vm, a, var);
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_store_closure_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	turkey_closure_set(vm, a, var);
}

void turkey_interpreter_instruction_store_closure_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;

	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	turkey_closure_set(vm, a, var);
}

void turkey_interpreter_instruction_store_closure_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = *(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;

	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	turkey_closure_set(vm, a, var);
}

void turkey_interpreter_instruction_new_array(TurkeyVM *vm) {
	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = TT_Array;
	ret.array = turkey_array_new(vm, (unsigned int)turkey_to_unsigned(vm, a));
	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_load_element(TurkeyVM *vm) {
	TurkeyVariable key, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(key)) key.type = TT_Null;

	TurkeyVariable element;

	switch(b.type) {
	default:
		element.type = TT_Null;
		break;
	case TT_Array:
		element = turkey_array_get_element(vm, b.array, (unsigned int)turkey_to_unsigned(vm, key));
		break;
	case TT_Object: {
		turkey_gc_hold(vm, b.object, TT_Object);
		element = turkey_object_get_property(vm, b.object, turkey_to_string(vm, key));
		turkey_gc_unhold(vm, b.object, TT_Object);
		}
		break;
	case TT_String: {
		unsigned long long int pos = turkey_to_unsigned(vm, key);
		if(pos >= b.string->length)
			element.type = TT_Null;
		else {
			element.type = TT_Unsigned;
			element.unsigned_value = (unsigned long long int)key.string->string[pos];
		}
		break; }
	}

	vm->variable_stack.Push(element);
}

void turkey_interpreter_instruction_save_element(TurkeyVM *vm) {
	TurkeyVariable key, b, element;
	if(!vm->variable_stack.Pop(element)) element.type = TT_Null;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(key)) key.type = TT_Null;

	switch(b.type) {
	default:
		break;
	case TT_Array:
		turkey_array_set_element(vm, b.array, (unsigned int)turkey_to_unsigned(vm, key), element);
		break;
	case TT_Object: {
		turkey_gc_hold(vm, b.object, TT_Object);
		turkey_gc_hold(vm, element);
		turkey_object_set_property(vm, b.object, turkey_to_string(vm, key), element);
		turkey_gc_unhold(vm, b.object, TT_Object);
		turkey_gc_unhold(vm, element);
		}
	}

}

void turkey_interpreter_instruction_new_object(TurkeyVM *vm) {
	TurkeyVariable ret;
	ret.type = TT_Object;
	ret.object = turkey_object_new(vm);
	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_delete_element(TurkeyVM *vm) {
	TurkeyVariable key, b;
	if(!vm->variable_stack.Pop(b)) b.type = TT_Null;
	if(!vm->variable_stack.Pop(key)) key.type = TT_Null;

	if(b.type != TT_Object)
		return;

	turkey_gc_hold(vm, b.object, TT_Object);
	turkey_object_delete_property(vm, b.object, turkey_to_string(vm, key));
	turkey_gc_unhold(vm, b.object, TT_Object);
}

void turkey_interpreter_instruction_new_buffer(TurkeyVM *vm) {
	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = TT_Buffer;
	ret.buffer = turkey_buffer_new(vm, (unsigned int)turkey_to_unsigned(vm, a));
	vm->variable_stack.Push(a);
}

void turkey_interpreter_instruction_load_buffer_unsigned_8(TurkeyVM *vm) {
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Unsigned;
		ret.unsigned_value = turkey_buffer_read_unsigned_8(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_load_buffer_unsigned_16(TurkeyVM *vm) {
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Unsigned;
		ret.unsigned_value = turkey_buffer_read_unsigned_16(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_load_buffer_unsigned_32(TurkeyVM *vm) {
	
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Unsigned;
		ret.unsigned_value = turkey_buffer_read_unsigned_32(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_load_buffer_unsigned_64(TurkeyVM *vm) {
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Unsigned;
		ret.unsigned_value = turkey_buffer_read_unsigned_64(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_store_buffer_unsigned_8(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_unsigned_8(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_unsigned(vm, value));
}

void turkey_interpreter_instruction_store_buffer_unsigned_16(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_unsigned_16(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_unsigned(vm, value));
}

void turkey_interpreter_instruction_store_buffer_unsigned_32(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_unsigned_32(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_unsigned(vm, value));
}

void turkey_interpreter_instruction_store_buffer_unsigned_64(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_unsigned_64(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_unsigned(vm, value));
}

void turkey_interpreter_instruction_load_buffer_signed_8(TurkeyVM *vm) {
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Signed;
		ret.signed_value = turkey_buffer_read_signed_8(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_load_buffer_signed_16(TurkeyVM *vm) {
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Signed;
		ret.signed_value = turkey_buffer_read_signed_16(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_load_buffer_signed_32(TurkeyVM *vm) {
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Signed;
		ret.signed_value = turkey_buffer_read_signed_32(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_load_buffer_signed_64(TurkeyVM *vm) {
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Signed;
		ret.signed_value = turkey_buffer_read_signed_64(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_store_buffer_signed_8(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_signed_8(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_signed(vm, value));
}

void turkey_interpreter_instruction_store_buffer_signed_16(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_signed_16(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_signed(vm, value));
}

void turkey_interpreter_instruction_store_buffer_signed_32(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_signed_32(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_signed(vm, value));
}

void turkey_interpreter_instruction_store_buffer_signed_64(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_signed_64(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_signed(vm, value));
}

void turkey_interpreter_instruction_load_buffer_float_32(TurkeyVM *vm) {
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Float;
		ret.float_value = turkey_buffer_read_float_32(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_load_buffer_float_64(TurkeyVM *vm) {
	TurkeyVariable address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;

	TurkeyVariable ret;
	if(buffer.type == TT_Buffer) {
		ret.type = TT_Float;
		ret.float_value = turkey_buffer_read_float_64(vm, buffer.buffer, turkey_to_unsigned(vm, address));
	} else
		ret.type = TT_Null;

	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_store_buffer_float_32(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_float_32(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_float(vm, value));
}

void turkey_interpreter_instruction_store_buffer_float_64(TurkeyVM *vm) {
	TurkeyVariable value, address, buffer;
	if(!vm->variable_stack.Pop(buffer)) buffer.type = TT_Null;
	if(!vm->variable_stack.Pop(address)) address.type = TT_Null;
	if(!vm->variable_stack.Pop(value)) value.type = TT_Null;

	if(buffer.type == TT_Buffer)
		turkey_buffer_write_float_64(vm, buffer.buffer, turkey_to_unsigned(vm, address), turkey_to_float(vm, value));
}

void turkey_interpreter_instruction_push_integer_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	long long int val = (long long int)*(signed char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;
	
	TurkeyVariable var;
	var.type = TT_Signed;
	var.signed_value = val;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_integer_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	long long int val = (long long int)*(signed short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;
	
	TurkeyVariable var;
	var.type = TT_Signed;
	var.signed_value = val;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_integer_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	long long int val = (long long int)*(int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;
	
	TurkeyVariable var;
	var.type = TT_Signed;
	var.signed_value = val;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_integer_64(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 7 >= vm->interpreter_state->code_end)
		return;

	long long int val = *(long long int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 8;
	
	TurkeyVariable var;
	var.type = TT_Signed;
	var.signed_value = val;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_to_integer(TurkeyVM *vm) {
	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;

	var.signed_value = turkey_to_signed(vm, var);
	var.type = TT_Signed;

	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_unsigned_integer_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned long long int val = (unsigned long long int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;
	
	TurkeyVariable var;
	var.type = TT_Unsigned;
	var.unsigned_value = val;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_unsigned_integer_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	unsigned long long int val = (unsigned long long int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;
	
	TurkeyVariable var;
	var.type = TT_Unsigned;
	var.unsigned_value = val;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_unsigned_integer_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned long long int val = (unsigned long long int)*(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;
	
	TurkeyVariable var;
	var.type = TT_Unsigned;
	var.unsigned_value = val;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_unsigned_integer_64(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 7 >= vm->interpreter_state->code_end)
		return;

	unsigned long long int val = *(unsigned long long int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 8;
	
	TurkeyVariable var;
	var.type = TT_Unsigned;
	var.unsigned_value = val;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_to_unsigned_integer(TurkeyVM *vm) {
	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;

	var.unsigned_value = turkey_to_unsigned(vm, var);
	var.type = TT_Unsigned;

	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_float(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 7 >= vm->interpreter_state->code_end)
		return;

	double val = *(double *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 8;
	
	TurkeyVariable var;
	var.type = TT_Float;
	var.float_value = val;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_to_float(TurkeyVM *vm) {
	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;

	var.float_value = turkey_to_float(vm, var);
	var.type = TT_Float;

	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_true(TurkeyVM *vm) {
	turkey_push_boolean(vm, true);
}

void turkey_interpreter_instruction_push_false(TurkeyVM *vm) {
	turkey_push_boolean(vm, false);
}

void turkey_interpreter_instruction_push_null(TurkeyVM *vm) {
	turkey_push_null(vm);
}

void turkey_interpreter_instruction_push_string_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	size_t a = (unsigned long long int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	TurkeyModule *module = vm->interpreter_state->function->module;
	TurkeyVariable var;

	if(a >= module->string_count)
		var.type = TT_Null;
	else {	
		var.type = TT_String;
		var.string = module->strings[a];
	}
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_string_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	size_t a = (unsigned long long int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;

	TurkeyModule *module = vm->interpreter_state->function->module;
	TurkeyVariable var;

	if(a >= module->string_count)
		var.type = TT_Null;
	else {	
		var.type = TT_String;
		var.string = module->strings[a];
	}
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_string_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	size_t a = (unsigned long long int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;

	TurkeyModule *module = vm->interpreter_state->function->module;
	TurkeyVariable var;

	if(a >= module->string_count)
		var.type = TT_Null;
	else {	
		var.type = TT_String;
		var.string = module->strings[a];
	}
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_push_function(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned int func_ind = *(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;

	TurkeyModule *module = vm->interpreter_state->function->module;
	TurkeyVariable ret;
	
	if(func_ind >= module->function_count)
		ret.type = TT_Null;
	else {
		ret.type = TT_FunctionPointer;
		ret.function = turkey_functionpointer_new(vm, module->functions[func_ind], vm->interpreter_state->closure);
	}
	
	vm->variable_stack.Push(ret);
}

void turkey_interpreter_instruction_call_function_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned int argc = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	TurkeyVariable func;
	if(!vm->variable_stack.Pop(func)) func.type = TT_Null;

	TurkeyVariable returnVar;

	if(func.type == TT_FunctionPointer)
		returnVar = turkey_call_function(vm, func.function, argc);
	else {
		while(argc > 0) {
			vm->variable_stack.PopNoReturn();
			argc--;
		}

		returnVar.type = TT_Null;
	}

	vm->variable_stack.Push(returnVar);
}

void turkey_interpreter_instruction_call_function_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1>= vm->interpreter_state->code_end)
		return;

	unsigned int argc = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;

	TurkeyVariable func;
	if(!vm->variable_stack.Pop(func)) func.type = TT_Null;

	TurkeyVariable returnVar;

	if(func.type == TT_FunctionPointer)
		returnVar = turkey_call_function(vm, func.function, argc);
	else {
		while(argc > 0) {
			vm->variable_stack.PopNoReturn();
			argc--;
		}

		returnVar.type = TT_Null;
	}

	vm->variable_stack.Push(returnVar);
}

void turkey_interpreter_instruction_call_function_no_return_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned int argc = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	TurkeyVariable func;
	if(!vm->variable_stack.Pop(func)) func.type = TT_Null;

	if(func.type == TT_FunctionPointer)
		turkey_call_function_no_return(vm, func.function, argc);
	else {
		while(argc > 0) {
			vm->variable_stack.PopNoReturn();
			argc--;
		}
	}
}

void turkey_interpreter_instruction_call_function_no_return_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1>= vm->interpreter_state->code_end)
		return;

	unsigned int argc = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;

	TurkeyVariable func;
	if(!vm->variable_stack.Pop(func)) func.type = TT_Null;

	if(func.type == TT_FunctionPointer)
		turkey_call_function(vm, func.function, argc);
	else {
		while(argc > 0) {
			vm->variable_stack.PopNoReturn();
			argc--;
		}
	}
}

void turkey_interpreter_instruction_return_null(TurkeyVM *vm) {
	TurkeyVariable var;
	var.type = TT_Null;
	vm->variable_stack.Push(var);
	vm->interpreter_state->executing = false;
}

void turkey_interpreter_instruction_return(TurkeyVM *vm) {
	vm->interpreter_state->executing = false;
}

void turkey_interpreter_instruction_get_type(TurkeyVM *vm) {
	TurkeyVariable var_in;
	if(!vm->variable_stack.Pop(var_in)) var_in.type = TT_Null;

	TurkeyString *str;

	switch(var_in.type) {
	case TT_Array: str = vm->string_table.s_array; break;
	case TT_Boolean: str = vm->string_table.s_boolean; break;
	case TT_Buffer: str = vm->string_table.s_buffer; break;
	case TT_Float: str = vm->string_table.s_float; break;
	case TT_Null:
	default: str = vm->string_table.s_null; break;
	case TT_FunctionPointer: str = vm->string_table.s_function; break;
	case TT_Unsigned: str = vm->string_table.s_unsigned; break;
	case TT_Object: str = vm->string_table.s_object; break;
	case TT_Signed: str = vm->string_table.s_signed; break;
	case TT_String: str = vm->string_table.s_string; break;
	}
	
	TurkeyVariable var;
	var.type = TT_String;
	var.string = str;

	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_jump_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	size_t ptr = (size_t)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
}

void turkey_interpreter_instruction_jump_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	size_t ptr = (size_t)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
}

void turkey_interpreter_instruction_jump_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	size_t ptr = (size_t)*(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
}

void turkey_interpreter_instruction_jump_if_true_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(turkey_to_boolean(vm, a)) {
		size_t ptr = (size_t)*(unsigned char *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr++;
}

void turkey_interpreter_instruction_jump_if_true_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(turkey_to_boolean(vm, a)) {
		size_t ptr = (size_t)*(unsigned short *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr += 2;
}

void turkey_interpreter_instruction_jump_if_true_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(turkey_to_boolean(vm, a)) {
		size_t ptr = (size_t)*(unsigned int *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr += 4;
}

void turkey_interpreter_instruction_jump_if_false_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(!turkey_to_boolean(vm, a)) {
		size_t ptr = (size_t)*(unsigned char *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr++;
}

void turkey_interpreter_instruction_jump_if_false_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(!turkey_to_boolean(vm, a)) {
		size_t ptr = (size_t)*(unsigned short *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr += 2;
}

void turkey_interpreter_instruction_jump_if_false_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(!turkey_to_boolean(vm, a)) {
		size_t ptr = (size_t)*(unsigned int *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr += 4;
}

void turkey_interpreter_instruction_jump_if_null_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(a.type == TT_Null) {
		size_t ptr = (size_t)*(unsigned char *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr++;
}

void turkey_interpreter_instruction_jump_if_null_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(a.type == TT_Null) {
		size_t ptr = (size_t)*(unsigned short *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr += 2;
}

void turkey_interpreter_instruction_jump_if_null_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(a.type == TT_Null) {
		size_t ptr = (size_t)*(unsigned int *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr += 4;
}

void turkey_interpreter_instruction_jump_if_not_null_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(a.type != TT_Null) {
		size_t ptr = (size_t)*(unsigned char *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr++;
}

void turkey_interpreter_instruction_jump_if_not_null_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(a.type != TT_Null) {
		size_t ptr = (size_t)*(unsigned short *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr += 2;
}

void turkey_interpreter_instruction_jump_if_not_null_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;
	if(a.type != TT_Null) {
		size_t ptr = (size_t)*(unsigned int *)vm->interpreter_state->code_ptr;
		vm->interpreter_state->code_ptr = vm->interpreter_state->code_start + ptr;
	} else
		vm->interpreter_state->code_ptr += 4;
}

void turkey_interpreter_instruction_require(TurkeyVM *vm) {
	turkey_require(vm);
}
/*
void turkey_interpreter_instruction_load_parameter_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	TurkeyVariable var;
	turkey_stack_get(vm->parameter_stack, a, var);
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_load_parameter_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;

	TurkeyVariable var;
	turkey_stack_get(vm->parameter_stack, a, var);
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_load_parameter_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = *(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;

	TurkeyVariable var;
	turkey_stack_get(vm->parameter_stack, a, var);
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_store_parameter_8(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned char *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr++;

	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	turkey_stack_set(vm->parameter_stack, a, var);
}

void turkey_interpreter_instruction_store_parameter_16(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 1 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = (unsigned int)*(unsigned short *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 2;

	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	turkey_stack_set(vm->parameter_stack, a, var);
}

void turkey_interpreter_instruction_store_parameter_32(TurkeyVM *vm) {
	if(vm->interpreter_state->code_ptr + 3 >= vm->interpreter_state->code_end)
		return;

	unsigned int a = *(unsigned int *)vm->interpreter_state->code_ptr;
	vm->interpreter_state->code_ptr += 4;

	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	turkey_stack_set(vm->parameter_stack, a, var);
}*/

void turkey_interpreter_instruction_to_string(TurkeyVM *vm) {
	TurkeyVariable var;
	if(!vm->variable_stack.Pop(var)) var.type = TT_Null;
	switch(var.type) {
	case TT_String:
		vm->variable_stack.Push(var);
		return; // do nothing
	case TT_Buffer:
	case TT_Object:
	case TT_Array:
		assert(0); // Not implemented!
		break;
	default:
		TurkeyString *str = turkey_to_string(vm, var);
		var.string = str;
		break;
	}

	var.type = TT_String;
	vm->variable_stack.Push(var);
}

void turkey_interpreter_instruction_invert(TurkeyVM *vm) {
	TurkeyVariable a;
	if(!vm->variable_stack.Pop(a)) a.type = TT_Null;

	TurkeyVariable ret;
	ret.type = a.type;
	/* convert to the type of the left */
	switch(a.type) {
	default:
		ret.type = TT_Null;
		break;
	case TT_Boolean:
		ret.boolean_value = !a.boolean_value;
		break;
	case TT_Float:
		ret.float_value = a.float_value * -1;
		break;
	case TT_Unsigned:
		ret.type = TT_Signed;
		ret.signed_value = (long long int)a.unsigned_value * -1;
		break;
	case TT_Object: {
		TurkeyVariable v;
		v.type = TT_Signed;
		v.signed_value = -1;
		turkey_object_call_operator(vm, a.object, vm->string_table.ss_multiply, v);
		return; }
	case TT_Signed:
		ret.signed_value = a.signed_value * -1;
		break;
	}

	vm->variable_stack.Push(ret);
}

TurkeyInstructionHandler turkey_interpreter_operations[256] = {
	turkey_interpreter_instruction_add, // 0
	turkey_interpreter_instruction_subtract, // 1
	turkey_interpreter_instruction_divide, // 2
	turkey_interpreter_instruction_multiply, // 3
	turkey_interpreter_instruction_modulo, // 4
	turkey_interpreter_instruction_increment, // 5
	turkey_interpreter_instruction_decrement, // 6
	turkey_interpreter_instruction_xor, // 7
	turkey_interpreter_instruction_and, // 8
	turkey_interpreter_instruction_or, // 9
	turkey_interpreter_instruction_not, // 10
	turkey_interpreter_instruction_shift_left, // 11
	turkey_interpreter_instruction_shift_right, // 12
	turkey_interpreter_instruction_rotate_left, // 13
	turkey_interpreter_instruction_rotate_right, // 14
	turkey_interpreter_instruction_is_null, // 15
	turkey_interpreter_instruction_is_not_null, // 16
	turkey_interpreter_instruction_equals, // 17
	turkey_interpreter_instruction_not_equals, // 18
	turkey_interpreter_instruction_less_than, // 19
	turkey_interpreter_instruction_greater_than, // 20
	turkey_interpreter_instruction_less_than_or_equals, // 21
	turkey_interpreter_instruction_greater_than_or_equals, // 22
	turkey_interpreter_instruction_is_true, // 23
	turkey_interpreter_instruction_is_false, // 24
	turkey_interpreter_instruction_pop, // 25
	turkey_interpreter_instruction_pop_many, // 26
	turkey_interpreter_instruction_grab_8, // 27
	turkey_interpreter_instruction_grab_16, // 28
	turkey_interpreter_instruction_grab_32, // 29
	turkey_interpreter_instruction_push_many_nulls, //turkey_interpreter_instruction_load_8, // 30
	turkey_interpreter_instruction_nop, //turkey_interpreter_instruction_load_16, // 31
	turkey_interpreter_instruction_nop, //turkey_interpreter_instruction_load_32, // 32
	turkey_interpreter_instruction_store_8, // 33
	turkey_interpreter_instruction_store_16, // 34
	turkey_interpreter_instruction_store_32, // 35
	turkey_interpreter_instruction_swap_8, // 36
	turkey_interpreter_instruction_swap_16, // 37
	turkey_interpreter_instruction_swap_32, // 38
	turkey_interpreter_instruction_load_closure_8, // 39
	turkey_interpreter_instruction_load_closure_16, // 40
	turkey_interpreter_instruction_load_closure_32, // 41
	turkey_interpreter_instruction_store_closure_8, // 42
	turkey_interpreter_instruction_store_closure_16, // 43
	turkey_interpreter_instruction_store_closure_32, // 44
	turkey_interpreter_instruction_new_array, // 45
	turkey_interpreter_instruction_load_element, // 46
	turkey_interpreter_instruction_save_element, // 47
	turkey_interpreter_instruction_new_object, // 48
	turkey_interpreter_instruction_delete_element, // 49
	turkey_interpreter_instruction_new_buffer, // 50
	turkey_interpreter_instruction_load_buffer_unsigned_8, // 51
	turkey_interpreter_instruction_load_buffer_unsigned_16, // 52
	turkey_interpreter_instruction_load_buffer_unsigned_32, // 53
	turkey_interpreter_instruction_load_buffer_unsigned_64, // 54
	turkey_interpreter_instruction_store_buffer_unsigned_8, // 55
	turkey_interpreter_instruction_store_buffer_unsigned_16, // 56
	turkey_interpreter_instruction_store_buffer_unsigned_32, // 57
	turkey_interpreter_instruction_store_buffer_unsigned_64, // 58
	turkey_interpreter_instruction_load_buffer_signed_8, // 59
	turkey_interpreter_instruction_load_buffer_signed_16, // 60
	turkey_interpreter_instruction_load_buffer_signed_32, // 61
	turkey_interpreter_instruction_load_buffer_signed_64, // 62
	turkey_interpreter_instruction_store_buffer_signed_8, // 63
	turkey_interpreter_instruction_store_buffer_signed_16, // 64
	turkey_interpreter_instruction_store_buffer_signed_32, // 65
	turkey_interpreter_instruction_store_buffer_signed_64, // 66
	turkey_interpreter_instruction_nop, // 67
	turkey_interpreter_instruction_load_buffer_float_32, // 68
	turkey_interpreter_instruction_load_buffer_float_64, // 69
	turkey_interpreter_instruction_nop, // 70
	turkey_interpreter_instruction_store_buffer_float_32, // 71
	turkey_interpreter_instruction_store_buffer_float_64, // 72
	turkey_interpreter_instruction_push_integer_8, // 73
	turkey_interpreter_instruction_push_integer_16, // 74
	turkey_interpreter_instruction_push_integer_32, // 75
	turkey_interpreter_instruction_push_integer_64, // 76
	turkey_interpreter_instruction_to_integer, // 77
	turkey_interpreter_instruction_push_unsigned_integer_8, // 78
	turkey_interpreter_instruction_push_unsigned_integer_16, // 79
	turkey_interpreter_instruction_push_unsigned_integer_32, // 80
	turkey_interpreter_instruction_push_unsigned_integer_64, // 81
	turkey_interpreter_instruction_to_unsigned_integer, // 82
	turkey_interpreter_instruction_push_float, // 83
	turkey_interpreter_instruction_to_float, // 84
	turkey_interpreter_instruction_push_true, // 85
	turkey_interpreter_instruction_push_false, // 86
	turkey_interpreter_instruction_push_null, // 87
	turkey_interpreter_instruction_push_string_8, // 88
	turkey_interpreter_instruction_push_string_16, // 89
	turkey_interpreter_instruction_push_string_32, // 90
	turkey_interpreter_instruction_push_function, // 91
	turkey_interpreter_instruction_call_function_8, // 92
	turkey_interpreter_instruction_call_function_16, // 93
	turkey_interpreter_instruction_call_function_no_return_8, // 94
	turkey_interpreter_instruction_call_function_no_return_16, // 95
	turkey_interpreter_instruction_return_null, // 96
	turkey_interpreter_instruction_return, // 97
	turkey_interpreter_instruction_get_type, // 98
	turkey_interpreter_instruction_jump_8, // 99
	turkey_interpreter_instruction_jump_16, // 100
	turkey_interpreter_instruction_jump_32, // 101
	turkey_interpreter_instruction_jump_if_true_8, // 102
	turkey_interpreter_instruction_jump_if_true_16, // 103
	turkey_interpreter_instruction_jump_if_true_32, // 104
	turkey_interpreter_instruction_jump_if_false_8, // 105
	turkey_interpreter_instruction_jump_if_false_16, // 106
	turkey_interpreter_instruction_jump_if_false_32, // 107
	turkey_interpreter_instruction_jump_if_null_8, // 108
	turkey_interpreter_instruction_jump_if_null_16, // 109
	turkey_interpreter_instruction_jump_if_null_32, // 110
	turkey_interpreter_instruction_jump_if_not_null_8, // 111
	turkey_interpreter_instruction_jump_if_not_null_16, // 112
	turkey_interpreter_instruction_jump_if_not_null_32, // 113
	turkey_interpreter_instruction_require, // 114
	turkey_interpreter_instruction_nop, // turkey_interpreter_instruction_load_parameter_8, // 115
	turkey_interpreter_instruction_nop, // turkey_interpreter_instruction_load_parameter_16, // 116
	turkey_interpreter_instruction_nop, // turkey_interpreter_instruction_load_parameter_32, // 117
	turkey_interpreter_instruction_nop, // turkey_interpreter_instruction_store_parameter_8, // 118
	turkey_interpreter_instruction_nop, // turkey_interpreter_instruction_store_parameter_16, // 119
	turkey_interpreter_instruction_nop, // turkey_interpreter_instruction_store_parameter_32, // 120
	turkey_interpreter_instruction_to_string, // 121
	turkey_interpreter_instruction_invert, // 122
	turkey_interpreter_instruction_call_function_8, // 123 - actually procedure not function, but they do the same thing in the interpreter
	turkey_interpreter_instruction_call_function_16, // 124 - ditto
	turkey_interpreter_instruction_nop, // 125
	turkey_interpreter_instruction_nop, // 126
	turkey_interpreter_instruction_nop, // 127
	turkey_interpreter_instruction_nop, // 128
	turkey_interpreter_instruction_nop, // 129
	turkey_interpreter_instruction_nop, // 130
	turkey_interpreter_instruction_nop, // 131
	turkey_interpreter_instruction_nop, // 132
	turkey_interpreter_instruction_nop, // 133
	turkey_interpreter_instruction_nop, // 134
	turkey_interpreter_instruction_nop, // 135
	turkey_interpreter_instruction_nop, // 136
	turkey_interpreter_instruction_nop, // 137
	turkey_interpreter_instruction_nop, // 138
	turkey_interpreter_instruction_nop, // 139
	turkey_interpreter_instruction_nop, // 140
	turkey_interpreter_instruction_nop, // 141
	turkey_interpreter_instruction_nop, // 142
	turkey_interpreter_instruction_nop, // 143
	turkey_interpreter_instruction_nop, // 144
	turkey_interpreter_instruction_nop, // 145
	turkey_interpreter_instruction_nop, // 146
	turkey_interpreter_instruction_nop, // 147
	turkey_interpreter_instruction_nop, // 148
	turkey_interpreter_instruction_nop, // 149
	turkey_interpreter_instruction_nop, // 150
	turkey_interpreter_instruction_nop, // 151
	turkey_interpreter_instruction_nop, // 152
	turkey_interpreter_instruction_nop, // 153
	turkey_interpreter_instruction_nop, // 154
	turkey_interpreter_instruction_nop, // 155
	turkey_interpreter_instruction_nop, // 156
	turkey_interpreter_instruction_nop, // 157
	turkey_interpreter_instruction_nop, // 158
	turkey_interpreter_instruction_nop, // 159
	turkey_interpreter_instruction_nop, // 160
	turkey_interpreter_instruction_nop, // 161
	turkey_interpreter_instruction_nop, // 162
	turkey_interpreter_instruction_nop, // 163
	turkey_interpreter_instruction_nop, // 164
	turkey_interpreter_instruction_nop, // 165
	turkey_interpreter_instruction_nop, // 166
	turkey_interpreter_instruction_nop, // 167
	turkey_interpreter_instruction_nop, // 168
	turkey_interpreter_instruction_nop, // 169
	turkey_interpreter_instruction_nop, // 170
	turkey_interpreter_instruction_nop, // 171
	turkey_interpreter_instruction_nop, // 172
	turkey_interpreter_instruction_nop, // 173
	turkey_interpreter_instruction_nop, // 174
	turkey_interpreter_instruction_nop, // 175
	turkey_interpreter_instruction_nop, // 176
	turkey_interpreter_instruction_nop, // 177
	turkey_interpreter_instruction_nop, // 178
	turkey_interpreter_instruction_nop, // 179
	turkey_interpreter_instruction_nop, // 180
	turkey_interpreter_instruction_nop, // 181
	turkey_interpreter_instruction_nop, // 182
	turkey_interpreter_instruction_nop, // 183
	turkey_interpreter_instruction_nop, // 184
	turkey_interpreter_instruction_nop, // 185
	turkey_interpreter_instruction_nop, // 186
	turkey_interpreter_instruction_nop, // 187
	turkey_interpreter_instruction_nop, // 188
	turkey_interpreter_instruction_nop, // 189
	turkey_interpreter_instruction_nop, // 190
	turkey_interpreter_instruction_nop, // 191
	turkey_interpreter_instruction_nop, // 192
	turkey_interpreter_instruction_nop, // 193
	turkey_interpreter_instruction_nop, // 194
	turkey_interpreter_instruction_nop, // 195
	turkey_interpreter_instruction_nop, // 196
	turkey_interpreter_instruction_nop, // 197
	turkey_interpreter_instruction_nop, // 198
	turkey_interpreter_instruction_nop, // 199
	turkey_interpreter_instruction_nop, // 200
	turkey_interpreter_instruction_nop, // 201
	turkey_interpreter_instruction_nop, // 202
	turkey_interpreter_instruction_nop, // 203
	turkey_interpreter_instruction_nop, // 204
	turkey_interpreter_instruction_nop, // 205
	turkey_interpreter_instruction_nop, // 206
	turkey_interpreter_instruction_nop, // 207
	turkey_interpreter_instruction_nop, // 208
	turkey_interpreter_instruction_nop, // 209
	turkey_interpreter_instruction_nop, // 210
	turkey_interpreter_instruction_nop, // 211
	turkey_interpreter_instruction_nop, // 212
	turkey_interpreter_instruction_nop, // 213
	turkey_interpreter_instruction_nop, // 214
	turkey_interpreter_instruction_nop, // 215
	turkey_interpreter_instruction_nop, // 216
	turkey_interpreter_instruction_nop, // 217
	turkey_interpreter_instruction_nop, // 218
	turkey_interpreter_instruction_nop, // 219
	turkey_interpreter_instruction_nop, // 220
	turkey_interpreter_instruction_nop, // 221
	turkey_interpreter_instruction_nop, // 222
	turkey_interpreter_instruction_nop, // 223
	turkey_interpreter_instruction_nop, // 224
	turkey_interpreter_instruction_nop, // 225
	turkey_interpreter_instruction_nop, // 226
	turkey_interpreter_instruction_nop, // 227
	turkey_interpreter_instruction_nop, // 228
	turkey_interpreter_instruction_nop, // 229
	turkey_interpreter_instruction_nop, // 230
	turkey_interpreter_instruction_nop, // 231
	turkey_interpreter_instruction_nop, // 232
	turkey_interpreter_instruction_nop, // 233
	turkey_interpreter_instruction_nop, // 234
	turkey_interpreter_instruction_nop, // 235
	turkey_interpreter_instruction_nop, // 236
	turkey_interpreter_instruction_nop, // 237
	turkey_interpreter_instruction_nop, // 238
	turkey_interpreter_instruction_nop, // 239
	turkey_interpreter_instruction_nop, // 240
	turkey_interpreter_instruction_nop, // 241
	turkey_interpreter_instruction_nop, // 242
	turkey_interpreter_instruction_nop, // 243
	turkey_interpreter_instruction_nop, // 244
	turkey_interpreter_instruction_nop, // 245
	turkey_interpreter_instruction_nop, // 246
	turkey_interpreter_instruction_nop, // 247
	turkey_interpreter_instruction_nop, // 248
	turkey_interpreter_instruction_nop, // 249
	turkey_interpreter_instruction_nop, // 250
	turkey_interpreter_instruction_nop, // 251
	turkey_interpreter_instruction_nop, // 252
	turkey_interpreter_instruction_nop, // 253
	turkey_interpreter_instruction_nop, // 254
	turkey_interpreter_instruction_nop // 255
};