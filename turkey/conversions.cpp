#include "turkey.h"
#include "hooks.h"

TurkeyString *turkey_to_string(TurkeyVM *vm, TurkeyVariable &var_in) {
	switch(var_in.type) {
	case TT_Array: {
		TurkeyArray *arr = var_in.array;
		turkey_gc_hold(vm, arr, TT_Array);
		
		TurkeyString *str = vm->string_table.ss_opening_bracket;

		for(unsigned int i = 0; i < arr->length; i++) {
			if(str != 0)
				str = turkey_string_append(vm, str, vm->string_table.ss_comma);

			TurkeyString *child;
			
			turkey_gc_hold(vm, str, TT_String);

			if(arr->elements[i].type == TT_String)
				child = turkey_string_escape(vm, arr->elements[i].string);
			else
				child = turkey_to_string(vm, arr->elements[i]);
			
			turkey_gc_unhold(vm, str, TT_String);
			str = turkey_string_append(vm, str, child);
		}

		str = turkey_string_append(vm, str, vm->string_table.ss_closing_bracket);

		turkey_gc_unhold(vm, arr, TT_Array);

		return str;
	}
	case TT_Boolean:
		if(var_in.boolean_value)
			return vm->string_table.s_true;
		else
			return vm->string_table.s_false;
	case TT_Buffer: {
		TurkeyBuffer *buf = var_in.buffer;
		turkey_gc_hold(vm, buf, TT_Buffer);
		
		char *str = (char *)turkey_allocate_memory(vm->tag, buf->size * 2 + 2);

		char *ptr = str;
		*ptr = '<';
		ptr++;

		char chars[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

		for(unsigned int i = 0; i < buf->size * 2; i++) {
			char b = ((char *)buf->ptr)[i];

			*ptr = chars[b >> 4];
			ptr++;
			*ptr = chars[b & 0xF];
			ptr++;
		}

		*ptr = ']';

		TurkeyString *toReturn = turkey_stringtable_newstring(vm, str, (unsigned int)buf->size * 2 + 2);
		turkey_free_memory(vm->tag, str, buf->size * 2 + 2);
		turkey_gc_unhold(vm, buf, TT_Buffer);
		
		return vm->string_table.s_buffer; }
	case TT_Float: {
		char temp[64];
		size_t len = 64;
		turkey_print_string(temp, len, "%f", var_in.float_value);
		return turkey_stringtable_newstring(vm, temp, (unsigned int)len);
		}
	case TT_Null:
	default:
		return vm->string_table.s_null;
	case TT_FunctionPointer:
		return vm->string_table.s_function;
	case TT_Unsigned: {
		char temp[64];
		size_t len = 64;
		turkey_print_string(temp, len, "%lu", var_in.unsigned_value);
		return turkey_stringtable_newstring(vm, temp, (unsigned int)len);
		}
	case TT_Object: {
		TurkeyObject *obj = var_in.object;
		turkey_gc_hold(vm, obj, TT_Object);
		
		TurkeyString *str = vm->string_table.ss_opening_brace;
		bool first = true;

		// iterate over everything in the object
		for(unsigned int i = 0; i < obj->size; i++) {
			TurkeyObjectProperty *prop = obj->properties[i];
			while(prop != 0) {
				if(first) {
					first = false;
					str = turkey_string_append(vm, str, vm->string_table.ss_comma);
				}

				// append key
				turkey_gc_hold(vm, str, TT_String);
				TurkeyString *child = turkey_string_escape(vm, prop->key);
				turkey_gc_unhold(vm, str, TT_String);
				str = turkey_string_append(vm, str, child);

				
				// append value
				turkey_gc_hold(vm, str, TT_String);
				if(prop->value.type == TT_String)
					child = turkey_string_escape(vm, prop->value.string);
				else
					child = turkey_to_string(vm, prop->value);

				turkey_gc_unhold(vm, str, TT_String);
				str = turkey_string_append(vm, str, child);

				prop = prop->next;
			}
		}

		str = turkey_string_append(vm, str, vm->string_table.ss_closing_bracket);

		turkey_gc_unhold(vm, obj, TT_Object);
		return str;
		}
	case TT_Signed: {
		char temp[64];
		size_t len = 64;
		turkey_print_string(temp, len, "%li", var_in.signed_value);
		return turkey_stringtable_newstring(vm, temp, (unsigned int)len);
		}
	case TT_String:
		return var_in.string;
	}
	
	
}

uint64_t turkey_to_unsigned(TurkeyVM *vm, TurkeyVariable &var_in) {
	switch(var_in.type) {
	case TT_Array:
	case TT_Buffer:
	case TT_FunctionPointer:
	case TT_Object:
	case TT_String:
		return 1;
	case TT_Null:
	default:
		return 0;
	case TT_Boolean:
		return var_in.boolean_value ? 1 : 0;
	case TT_Float:
		return (uint64_t)var_in.float_value;
	case TT_Signed:
		return (uint64_t)var_in.signed_value;
	case TT_Unsigned:
		return var_in.unsigned_value;
	}
}

long long int turkey_to_signed(TurkeyVM *vm, TurkeyVariable &var_in) {
	switch(var_in.type) {
	case TT_Array:
	case TT_Buffer:
	case TT_FunctionPointer:
	case TT_Object:
	case TT_String:
		return 1;
	case TT_Null:
	default:
		return 0;
	case TT_Boolean:
		return var_in.boolean_value ? 1 : 0;
	case TT_Float:
		return (long long int)var_in.float_value;
	case TT_Signed:
		return var_in.signed_value;
	case TT_Unsigned:
		return (long long int)var_in.unsigned_value;
	}
}

double turkey_to_float(TurkeyVM *vm, TurkeyVariable &var_in) {
	switch(var_in.type) {
	case TT_Array:
	case TT_Buffer:
	case TT_FunctionPointer:
	case TT_Object:
	case TT_String:
		return 1;
	case TT_Null:
	default:
		return 0;
	case TT_Boolean:
		return var_in.boolean_value ? 1 : 0;
	case TT_Float:
		return var_in.float_value;
	case TT_Signed:
		return (double)var_in.signed_value;
	case TT_Unsigned:
		return (double)var_in.unsigned_value;
	}
}

bool turkey_to_boolean(TurkeyVM *vm, TurkeyVariable &var_in) {
	switch(var_in.type) {
	case TT_Array:
	case TT_Buffer:
	case TT_FunctionPointer:
	case TT_Object:
	case TT_String:
		return 1;
	case TT_Null:
	default:
		return 0;
	case TT_Boolean:
		return var_in.boolean_value;
	case TT_Float:
		return var_in.float_value == 0.0 ? false : true;
	case TT_Signed:
		return var_in.signed_value == 0 ? false : true;
	case TT_Unsigned:
		return var_in.unsigned_value == 0 ? false : true;
	}
}