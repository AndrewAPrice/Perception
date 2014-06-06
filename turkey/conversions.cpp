#include "turkey_internal.h"
#include "hooks.h"

TurkeyString *turkey_to_string(TurkeyVM *vm, TurkeyVariable &var_in) {
	char temp[128];
	char *c;
	unsigned int len = 0;

	switch(var_in.type) {
	case TT_Array:
		c = "array";
		len = 5;
		break;
	case TT_Boolean:
		if(var_in.boolean_value) {
			c = "true";
			len = 4;
		} else {
			c = "false";
			len = 5;
		}
		break;
	case TT_Buffer:
		c = "buffer";
		len = 6;
	case TT_Float:
		len = 128;
		turkey_print_string(temp, len, "%f", var_in.float_value);
		break;
	case TT_Null:
	default:
		c = "null";
		len = 4;
		break;
	case TT_FunctionPointer:
		c = "function";
		len = 8;
		break;
	case TT_Unsigned:
		len = 128;
		turkey_print_string(temp, len, "%lu", var_in.unsigned_value);
		break;
	case TT_Object:
		c = "object";
		len = 6;
		break;
	case TT_Signed:
		len = 128;
		turkey_print_string(temp, len, "%li", var_in.unsigned_value);
		break;
	case TT_String:
		return var_in.string;
	}
	
	return turkey_stringtable_newstring(vm, c, len);
}

unsigned long long int turkey_to_unsigned(TurkeyVM *vm, TurkeyVariable &var_in) {
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
		return (unsigned long long int)var_in.float_value;
	case TT_Signed:
		return (unsigned long long int)var_in.signed_value;
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