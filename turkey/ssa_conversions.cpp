#include "turkey.h"

TurkeyString *turkey_ssa_to_string(TurkeyVM *vm, TurkeyInstruction &instruction) {
	switch(instruction.instruction) {
	default:
	case turkey_ir_null:
		return vm->string_table.ss_blank;
	case turkey_ir_true:
		return vm->string_table.s_true;
	case turkey_ir_false:
		return vm->string_table.s_false;
	case turkey_ir_float: {
		char temp[64];
		size_t len = 64;
		turkey_print_string(temp, len, "%f", *(double *)instruction.large);
		return turkey_stringtable_newstring(vm, temp, (unsigned int)len);
		}
	case turkey_ir_unsigned_integer: {
		char temp[64];
		size_t len = 64;
		turkey_print_string(temp, len, "%lu", instruction.large);
		return turkey_stringtable_newstring(vm, temp, (unsigned int)len);
		}
	case turkey_ir_signed_integer: {
		char temp[64];
		size_t len = 64;
		turkey_print_string(temp, len, "%li", *(signed signed int *)instruction.large);
		return turkey_stringtable_newstring(vm, temp, (unsigned int)len);
		}
	case turkey_ir_string:
		return (TurkeyString *)instruction.large;
	}
}

uint64_t turkey_ssa_to_unsigned(TurkeyVM *vm, TurkeyInstruction &instruction) {
	switch(instruction.instruction) {
	default:
	case turkey_ir_null:
		return 0;
	case turkey_ir_true:
		return 1;
	case turkey_ir_false:
		return 0;
	case turkey_ir_float:
		return (uint64_t)*(double *)instruction.large;
	case turkey_ir_unsigned_integer:
		return instruction.large;
	case turkey_ir_signed_integer:
		return (uint64_t)*(signed long long int *)instruction.large;
	case turkey_ir_string:
		return 1;
	}
}

long long int turkey_ssa_to_signed(TurkeyVM *vm, TurkeyInstruction &instruction) {
	switch(instruction.instruction) {
	default:
	case turkey_ir_null:
		return 0;
	case turkey_ir_true:
		return 1;
	case turkey_ir_false:
		return 0;
	case turkey_ir_float:
		return (signed long long int)*(double *)instruction.large;
	case turkey_ir_unsigned_integer:
		return (signed long long int)*(uint64_t *)instruction.large;
	case turkey_ir_signed_integer:
		return *(signed long long int *)instruction.large;
	case turkey_ir_string:
		return 1;
	}
}

double turkey_ssa_to_float(TurkeyVM *vm, TurkeyInstruction &instruction) {
	switch(instruction.instruction) {
	default:
	case turkey_ir_null:
		return 0.0;
	case turkey_ir_true:
		return 1.0;
	case turkey_ir_false:
		return 0.0;
	case turkey_ir_float:
		return *(double *)instruction.large;
	case turkey_ir_unsigned_integer:
		return (double)*(uint64_t *)instruction.large;
	case turkey_ir_signed_integer:
		return (double)*(signed long long int *)instruction.large;
	case turkey_ir_string:
		return 1.0;
	}
}

bool turkey_ssa_to_boolean(TurkeyVM *vm, TurkeyInstruction &instruction) {
	switch(instruction.instruction) {
	default:
	case turkey_ir_null:
		return false;
	case turkey_ir_true:
		return true;
	case turkey_ir_false:
		return false;
	case turkey_ir_float:
		return *(double *)instruction.large == 0.0 ? false : true;
	case turkey_ir_unsigned_integer:
		return *(uint64_t *)instruction.large ? false : true;
	case turkey_ir_signed_integer:
		return *(signed long long int *)instruction.large ? false : true;
	case turkey_ir_string:
		return true;
	}
}
