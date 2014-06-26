#include "turkey.h"
#ifdef DEBUG
#include <stdio.h>
#endif

/* optimizes SSA form, compile with OPTIMIZE_SSA to optimize SSA form after loading a module - there is a preprocessor line in
   ssa_optimizer.cpp for this */

#ifdef OPTIMIZE_SSA

bool turkey_ssa_optimizer_is_constant(const TurkeyInstruction &instruction) {
	switch(instruction.instruction) {
	case turkey_ir_unsigned_integer:
	case turkey_ir_signed_integer:
	case turkey_ir_float:
	case turkey_ir_null:
	case turkey_ir_true:
	case turkey_ir_false:
	case turkey_ir_string:
		return true;
	default:
		return false;
	}
}

bool turkey_ssa_optimizer_is_constant_number(const TurkeyInstruction &instruction) {
	/* things that can be turned into constant numbers, like arrays, objects, etc */
	switch(instruction.instruction) {
	case turkey_ir_unsigned_integer:
	case turkey_ir_signed_integer:
	case turkey_ir_float:
	case turkey_ir_null:
	case turkey_ir_true:
	case turkey_ir_false:
	case turkey_ir_string:
		return true;
	default:
		switch(instruction.return_type & TT_Mask) {
		case TT_Array:
		case TT_Buffer:
		case TT_FunctionPointer:
		case TT_Object:
		case TT_Null:
			return true;
		default:
			return false;
		}
	}
}

bool turkey_ssa_optimizer_is_constant_string(const TurkeyInstruction &instruction) {
	/* things that can be turned into constant numbers, like arrays, objects, etc */
	switch(instruction.instruction) {
	case turkey_ir_unsigned_integer:
	case turkey_ir_signed_integer:
	case turkey_ir_float:
	case turkey_ir_null:
	case turkey_ir_true:
	case turkey_ir_false:
	case turkey_ir_string:
		return true;
	default:
		return (instruction.return_type & TT_Mask) == TT_FunctionPointer;
	}
}

struct ssa_param_scan_item {
	unsigned int basic_block;
	unsigned int instruction;
};

struct ssa_param_scan {
	TurkeyStack<ssa_param_scan_item> scan_items;
};

void turkey_ssa_optimizer_touch_instruction(TurkeyVM *vm, TurkeyFunction *function, unsigned int bb, unsigned int inst) {
	/* touches an instruction, and finds any dependencies and marks them if needed, but does not actually mark _this_
		instruction (that's the caller's responsibility) because for things like constant propagation we may inline a
		constant value but don't want to 'mark' it */

	TurkeyBasicBlock &basic_block = function->basic_blocks[bb];
	TurkeyInstruction &instruction = basic_block.instructions[inst];

	if(instruction.return_type & TT_Marked)
		return;

	switch(instruction.instruction) {
	case turkey_ir_add: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Array:
			if((b.return_type & TT_Mask) == TT_Array) {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
				instruction.return_type = TT_Array;
			} else if((b.return_type & TT_Mask) == TT_Unknown) {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
				instruction.return_type = TT_Unknown;
			} else {
				instruction.instruction = turkey_ir_null; instruction.large = 0;
				instruction.return_type = TT_Null;
			}
			break;
		case TT_Boolean:
			instruction.return_type = TT_Boolean;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool bv = turkey_ssa_to_boolean(vm, b);
				bool res = av || bv;
				instruction.instruction = res ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
			instruction.return_type = TT_Float;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				double av = turkey_ssa_to_float(vm, a);
				double bv = turkey_ssa_to_float(vm, b);
				double result = av + bv;
				*(double *)&instruction.large = result;
				instruction.instruction = turkey_ir_float;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av + bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int bv = turkey_ssa_to_signed(vm, b);
				signed long long int result = av + bv;
				*(signed long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_String:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_string(a) && turkey_ssa_optimizer_is_constant_string(b)) {
				TurkeyString *av = turkey_ssa_to_string(vm, a);
				TurkeyString *bv = turkey_ssa_to_string(vm, b);
				TurkeyString *result = turkey_string_append(vm, av, bv);
				turkey_gc_hold(vm, result, TT_String);
				
				instruction.large = (unsigned long long int)result;
				instruction.instruction = turkey_ir_string;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_subtract: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			instruction.return_type = TT_Boolean;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool bv = turkey_ssa_to_boolean(vm, b);
				bool res = av && bv;
				instruction.instruction = res ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
			instruction.return_type = TT_Float;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				double av = turkey_ssa_to_float(vm, a);
				double bv = turkey_ssa_to_float(vm, b);
				double result = av - bv;
				*(double *)&instruction.large = result;
				instruction.instruction = turkey_ir_float;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av - bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int bv = turkey_ssa_to_signed(vm, b);
				signed long long int result = av - bv;
				*(signed long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_divide: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Float:
			instruction.return_type = TT_Float;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				double av = turkey_ssa_to_float(vm, a);
				double bv = turkey_ssa_to_float(vm, b);
				double result = av / bv;
				*(double *)&instruction.large = result;
				instruction.instruction = turkey_ir_float;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av / bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int bv = turkey_ssa_to_signed(vm, b);
				signed long long int result = av / bv;
				*(signed long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_multiply: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Float:
			instruction.return_type = TT_Float;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				double av = turkey_ssa_to_float(vm, a);
				double bv = turkey_ssa_to_float(vm, b);
				double result = av * bv;
				*(double *)&instruction.large = result;
				instruction.instruction = turkey_ir_float;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av * bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int bv = turkey_ssa_to_signed(vm, b);
				signed long long int result = av * bv;
				*(signed long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_modulo: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Float:
			instruction.return_type = TT_Float;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				double av = turkey_ssa_to_float(vm, a);
				double bv = turkey_ssa_to_float(vm, b);
				double result = turkey_float_modulo(av, bv);
				*(double *)&instruction.large = result;
				instruction.instruction = turkey_ir_float;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av % bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int bv = turkey_ssa_to_signed(vm, b);
				signed long long int result = av % bv;
				*(signed long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_invert: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			instruction.return_type = TT_Boolean;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool result = !av;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
			instruction.return_type = TT_Float;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				double av = turkey_ssa_to_float(vm, a);
				double result = av * -1;
				*(double *)&instruction.large = result;
				instruction.instruction = turkey_ir_float;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int result = av * -1;
				*(signed long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int result = av * -1;
				*(signed long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_increment: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			instruction.return_type = TT_Boolean;
			instruction.instruction = turkey_ir_true; instruction.large = 0;
			break;
		case TT_Float:
			instruction.return_type = TT_Float;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				double av = turkey_ssa_to_float(vm, a);
				double result = av + 1.0;
				*(double *)&instruction.large = result;
				instruction.instruction = turkey_ir_float;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int result = av + 1;
				*(unsigned long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				unsigned long long int av = turkey_ssa_to_signed(vm, a);
				unsigned long long int result = av + 1;
				*(unsigned long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_decrement: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			instruction.return_type = TT_Boolean;
			instruction.instruction = turkey_ir_false;
			instruction.large = 0;
			break;
		case TT_Float:
			instruction.return_type = TT_Float;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				double av = turkey_ssa_to_float(vm, a);
				double result = av - 1.0;
				*(double *)&instruction.large = result;
				instruction.instruction = turkey_ir_float;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int result = av - 1;
				*(unsigned long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				unsigned long long int av = turkey_ssa_to_signed(vm, a);
				unsigned long long int result = av - 1;
				*(unsigned long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_xor: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			instruction.return_type = TT_Boolean;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool bv = turkey_ssa_to_boolean(vm, b);
				bool res = av ^ bv;
				instruction.instruction = res ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				long long int av = turkey_ssa_to_signed(vm, a);
				long long int bv = turkey_ssa_to_signed(vm, b);
				long long int result = av ^ bv;
				*(long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av ^ bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		}
		break; }
	case turkey_ir_and: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			instruction.return_type = TT_Boolean;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool bv = turkey_ssa_to_boolean(vm, b);
				bool res = av & bv;
				instruction.instruction = res ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				long long int av = turkey_ssa_to_signed(vm, a);
				long long int bv = turkey_ssa_to_signed(vm, b);
				long long int result = av & bv;
				*(long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av & bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		}
		break; }
	case turkey_ir_or: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			instruction.return_type = TT_Boolean;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool bv = turkey_ssa_to_boolean(vm, b);
				bool res = av | bv;
				instruction.instruction = res ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				long long int av = turkey_ssa_to_signed(vm, a);
				long long int bv = turkey_ssa_to_signed(vm, b);
				long long int result = av | bv;
				*(long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av | bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		}
		break; }
	case turkey_ir_not: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			instruction.return_type = TT_Boolean;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool result = !av;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				long long int av = turkey_ssa_to_signed(vm, a);
				long long int result = ~av;
				*(long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown: {
			a.return_type |= TT_Marked;
			break; }
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a)) {
				unsigned long long int av = turkey_ssa_to_signed(vm, a);
				unsigned long long int result = ~av;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		}
		break; }
	case turkey_ir_shift_left: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
		case TT_Float:
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				long long int av = turkey_ssa_to_signed(vm, a);
				long long int bv = turkey_ssa_to_signed(vm, b);
				long long int result = av << bv;
				*(long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av << bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		}
		break; }
	case turkey_ir_shift_right: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
		case TT_Float:
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				long long int av = turkey_ssa_to_signed(vm, a);
				long long int bv = turkey_ssa_to_signed(vm, b);
				long long int result = av << bv;
				*(long long int *)&instruction.large = result;
				instruction.instruction = turkey_ir_signed_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = av >> bv;
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		}
		break; }
	case turkey_ir_rotate_left: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
		case TT_Float:
		case TT_Signed:
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = (av << bv) | (av >> (64 - bv));
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		}
		break; }
	case turkey_ir_rotate_right: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
		case TT_Float:
		case TT_Signed:
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				unsigned long long int result = (av >> bv) | (av << (64 - bv));
				instruction.large = result;
				instruction.instruction = turkey_ir_unsigned_integer;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		}
		break; }
	case turkey_ir_is_null: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		char type_a = a.return_type & TT_Mask;
		instruction.return_type = TT_Boolean;
		if(type_a == TT_Unknown)
			a.return_type |= TT_Marked;
		else if(type_a == TT_Null)
			instruction.instruction = turkey_ir_true;
		else
			instruction.instruction = turkey_ir_false;
		instruction.large = 0;
		break; }
	case turkey_ir_is_not_null: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		char type_a = a.return_type & TT_Mask;
		instruction.return_type = TT_Boolean;
		if(type_a == TT_Unknown)
			a.return_type |= TT_Marked;
		else if(type_a == TT_Null)
			instruction.instruction = turkey_ir_false;
		else
			instruction.instruction = turkey_ir_true;
		instruction.large = 0;
		break; }
	case turkey_ir_equals: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		instruction.return_type = TT_Boolean;

		int type_a = a.return_type & TT_Mask;
		int type_b = b.return_type & TT_Mask;

		if(type_a == TT_Unknown || type_b == TT_Unknown) {
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
		} else if (a.instruction == turkey_ir_string && b.instruction == turkey_ir_string) {
			/* compare two constant strings */
			bool result = a.large == b.large;
			instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
		} else if(TURKEY_IS_TYPE_NUMBER(type_a) && TURKEY_IS_TYPE_NUMBER(type_b)) {
			if(turkey_ssa_optimizer_is_constant(a) && turkey_ssa_optimizer_is_constant(b)) {
				bool result;

				if(type_a == TT_Float || type_b == TT_Float)
					result = turkey_ssa_to_float(vm, a) == turkey_ssa_to_float(vm, b);
				else if(type_a == TT_Signed || type_b == TT_Signed)
					result = turkey_ssa_to_signed(vm, a) ==  turkey_ssa_to_signed(vm, b);
				else if(type_a == TT_Unsigned || type_b == TT_Unsigned)
					result = turkey_ssa_to_unsigned(vm, a) == turkey_ssa_to_unsigned(vm, b);
				else
					result = turkey_ssa_to_boolean(vm, a) == turkey_ssa_to_boolean(vm, b);

				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
		} else if(type_a != type_b) {
			instruction.instruction = turkey_ir_false;
			instruction.large = 0;
		} else if(type_a == TT_Null) { // all null = null
			instruction.instruction = turkey_ir_true; instruction.large = 0;
		} else {
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
		}
		break; }
	case turkey_ir_not_equals: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		instruction.return_type = TT_Boolean;

		int type_a = a.return_type & TT_Mask;
		int type_b = b.return_type & TT_Mask;

		if(type_a == TT_Unknown || type_b == TT_Unknown) {
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
		} else if (a.instruction == turkey_ir_string && b.instruction == turkey_ir_string) {
			/* compare two constant strings */
			bool result = a.large != b.large;
			instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
		} else if(TURKEY_IS_TYPE_NUMBER(type_a) && TURKEY_IS_TYPE_NUMBER(type_b)) {
			if(turkey_ssa_optimizer_is_constant(a) && turkey_ssa_optimizer_is_constant(b)) {
				bool result;

				if(type_a == TT_Float || type_b == TT_Float)
					result = turkey_ssa_to_float(vm, a) != turkey_ssa_to_float(vm, b);
				else if(type_a == TT_Signed || type_b == TT_Signed)
					result = turkey_ssa_to_signed(vm, a) !=  turkey_ssa_to_signed(vm, b);
				else if(type_a == TT_Unsigned || type_b == TT_Unsigned)
					result = turkey_ssa_to_unsigned(vm, a) != turkey_ssa_to_unsigned(vm, b);
				else
					result = turkey_ssa_to_boolean(vm, a) != turkey_ssa_to_boolean(vm, b);

				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
		} else if(type_a != type_b) {
			instruction.instruction = turkey_ir_true; instruction.large = 0;
		} else if(type_a == TT_Null) { // all null = null
			instruction.instruction = turkey_ir_false;  instruction.large = 0;
		} else {
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
		}
		break; }
	case turkey_ir_less_than: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		instruction.return_type = TT_Boolean;

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool bv = turkey_ssa_to_boolean(vm, b);
				bool result = av && !bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				double av = turkey_ssa_to_float(vm, a);
				double bv = turkey_ssa_to_float(vm, b);
				bool result = av < bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				bool result = av < bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int bv = turkey_ssa_to_signed(vm, b);
				bool result = av < bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_greater_than: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		instruction.return_type = TT_Boolean;

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool bv = turkey_ssa_to_boolean(vm, b);
				bool result = !av && bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				double av = turkey_ssa_to_float(vm, a);
				double bv = turkey_ssa_to_float(vm, b);
				bool result = av > bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				bool result = av > bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int bv = turkey_ssa_to_signed(vm, b);
				bool result = av > bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_less_than_or_equals: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		instruction.return_type = TT_Boolean;

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				bool av = turkey_ssa_to_boolean(vm, a);
				bool result = !av;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				double av = turkey_ssa_to_float(vm, a);
				double bv = turkey_ssa_to_float(vm, b);
				bool result = av <= bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				bool result = av <= bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int bv = turkey_ssa_to_signed(vm, b);
				bool result = av <= bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_greater_than_or_equals: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];

		instruction.return_type = TT_Boolean;

		switch(a.return_type & TT_Mask) {
		case TT_Boolean:
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				bool bv = turkey_ssa_to_boolean(vm, b);
				bool result = !bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Float:
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				double av = turkey_ssa_to_float(vm, a);
				double bv = turkey_ssa_to_float(vm, b);
				bool result = av >= bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Unknown:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			break;
		default:
			instruction.instruction = turkey_ir_null; instruction.large = 0;
			instruction.return_type = TT_Null;
			break;
		case TT_Unsigned:
			instruction.return_type = TT_Unsigned;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
				unsigned long long int bv = turkey_ssa_to_unsigned(vm, b);
				bool result = av >= bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		case TT_Object:
			a.return_type |= TT_Marked;
			b.return_type |= TT_Marked;
			instruction.return_type = TT_Object;
			break;
		case TT_Signed:
			instruction.return_type = TT_Signed;
			if(turkey_ssa_optimizer_is_constant_number(a) && turkey_ssa_optimizer_is_constant_number(b)) {
				signed long long int av = turkey_ssa_to_signed(vm, a);
				signed long long int bv = turkey_ssa_to_signed(vm, b);
				bool result = av >= bv;
				instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
			} else {
				a.return_type |= TT_Marked;
				b.return_type |= TT_Marked;
			}
			break;
		}
		break; }
	case turkey_ir_is_true: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		instruction.return_type = TT_Boolean;
		if(turkey_ssa_optimizer_is_constant_number(a)) {
			bool result = turkey_ssa_to_boolean(vm, a);
			instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
		} else {
			a.return_type |= TT_Marked;
		}
		break; }
	case turkey_ir_is_false: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);

		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		instruction.return_type = TT_Boolean;
		if(turkey_ssa_optimizer_is_constant_number(a)) {
			bool result = !turkey_ssa_to_boolean(vm, a);
			instruction.instruction = result ? turkey_ir_true : turkey_ir_false; instruction.large = 0;
		} else {
			a.return_type |= TT_Marked;
		}
		break; }
	case turkey_ir_parameter: {
		/* mark me - why? because parameters scan across blocks (not linearly) and so may start getting stuck recursively if we
			don't do this */
		instruction.return_type |= TT_Marked;

		unsigned int param_num = instruction.a;
		
		bool constant = true;
		TurkeyInstruction ti;
		unsigned int i = 0;
		for(; i < basic_block.entry_point_count; i++) {
			// scan each entry point
			unsigned int entry_point_bb = basic_block.entry_points[i];
			TurkeyBasicBlock &entry_point_basic_block = function->basic_blocks[entry_point_bb];
			unsigned int local_inst = entry_point_basic_block.instructions_count - param_num - 1;

			turkey_ssa_optimizer_touch_instruction(vm, function, entry_point_bb, local_inst);
			
			TurkeyInstruction &a = entry_point_basic_block.instructions[local_inst];
			
			// is this parameter a constant?
			if(turkey_ssa_optimizer_is_constant(a)) {
				if(i == 0) // first entry point, no need to test
					ti = a;
				else if(ti.instruction != a.instruction || ti.large != a.large)
					// compare if it's the same as the other
					constant = false;
			} else
				constant = false; // not a constant

			a.return_type |= TT_Marked;

			// TODO: scan each exit point of the entry point and mark the parameter as needed
		}
		
		// we are propagating a constant
		if(constant && i > 0) { // check i > 0 because it might not be initialized for function parameters
			instruction.instruction = ti.instruction;
			instruction.large = ti.large;
			instruction.return_type = ti.return_type & TT_Mask;

			if(instruction.instruction == turkey_ir_string)
				turkey_gc_hold(vm, (TurkeyString *)instruction.large, TT_String);
		}
		break; }
	case turkey_ir_load_closure:
		break;
	case turkey_ir_store_closure: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		b.return_type |= TT_Marked;
		break; }
	case turkey_ir_new_array: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		a.return_type |= TT_Marked;
		break; }
	case turkey_ir_load_buffer_unsigned_8:
	case turkey_ir_load_buffer_unsigned_16:
	case turkey_ir_load_buffer_unsigned_32:
	case turkey_ir_load_buffer_unsigned_64:
	case turkey_ir_load_buffer_signed_8:
	case turkey_ir_load_buffer_signed_16:
	case turkey_ir_load_buffer_signed_32:
	case turkey_ir_load_buffer_signed_64:
	case turkey_ir_load_buffer_float_32:
	case turkey_ir_load_buffer_float_64:
	case turkey_ir_load_element: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		a.return_type |= TT_Marked;
		b.return_type |= TT_Marked;
		break; }
	case turkey_ir_store_buffer_unsigned_8:
	case turkey_ir_store_buffer_unsigned_16:
	case turkey_ir_store_buffer_unsigned_32:
	case turkey_ir_store_buffer_unsigned_64:
	case turkey_ir_store_buffer_signed_8:
	case turkey_ir_store_buffer_signed_16:
	case turkey_ir_store_buffer_signed_32:
	case turkey_ir_store_buffer_signed_64:
	case turkey_ir_store_buffer_float_32:
	case turkey_ir_store_buffer_float_64:
	case turkey_ir_save_element: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		a.return_type |= TT_Marked;
		b.return_type |= TT_Marked;

		TurkeyInstruction &param = basic_block.instructions[inst - 1];
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, param.a);
		TurkeyInstruction &pa = basic_block.instructions[param.a];
		pa.return_type |= TT_Marked;
		param.return_type = pa.return_type;

		break; }
	case turkey_ir_new_object:
		break;
	case turkey_ir_delete_element: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		a.return_type |= TT_Marked;
		b.return_type |= TT_Marked;
		break; }
	case turkey_ir_new_buffer: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		a.return_type |= TT_Marked;
		break; }
	case turkey_ir_signed_integer:
		instruction.return_type = TT_Signed;
		break;
	case turkey_ir_to_signed_integer: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		instruction.return_type = TT_Signed;
		if(turkey_ssa_optimizer_is_constant_number(a)) {
			long long int av = turkey_ssa_to_signed(vm, a);
			instruction.instruction = turkey_ir_signed_integer;
			*(long long int *)&instruction.large = av;
		} else
			a.return_type |= TT_Marked;
		break; }
	case turkey_ir_unsigned_integer:
		instruction.return_type = TT_Unsigned;
		break;
	case turkey_ir_to_unsigned_integer: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		instruction.return_type = TT_Unsigned;
		if(turkey_ssa_optimizer_is_constant_number(a)) {
			unsigned long long int av = turkey_ssa_to_unsigned(vm, a);
			instruction.instruction = turkey_ir_unsigned_integer;
			instruction.large = av;
		} else
			a.return_type |= TT_Marked;
		break; }
	case turkey_ir_float:
		instruction.return_type = TT_Float;
		break;
	case turkey_ir_to_float: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		instruction.return_type = TT_Float;
		if(turkey_ssa_optimizer_is_constant_number(a)) {
			double av = turkey_ssa_to_float(vm, a);
			instruction.instruction = turkey_ir_float;
			*(double *)&instruction.large = av;
		} else
			a.return_type |= TT_Marked;
		break; }
	case turkey_ir_true:
		instruction.return_type = TT_Boolean;
		break;
	case turkey_ir_false:
		instruction.return_type = TT_Boolean;
		break;
	case turkey_ir_null:
		instruction.return_type = TT_Null;
		break;
	case turkey_ir_string:
		instruction.return_type = TT_String;
		break;
	case turkey_ir_to_string: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		instruction.return_type = TT_String;
		if(turkey_ssa_optimizer_is_constant_string(a)) {
			TurkeyString *av = turkey_ssa_to_string(vm, a);
			turkey_gc_hold(vm, av, TT_String);
			instruction.instruction = turkey_ir_string;
			instruction.large = (size_t)av;
		} else
			a.return_type |= TT_Marked;
		break; }
	case turkey_ir_function:
		instruction.return_type = TT_FunctionPointer;
		break;
	case turkey_ir_call_function:
	case turkey_ir_call_function_no_return:
	case turkey_ir_call_pure_function: {
		unsigned int params = instruction.a;
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		b.return_type |= TT_Marked;

		for(unsigned int i = 0; i < params; i++) {			
			TurkeyInstruction &param_instruction = basic_block.instructions[inst - i - 1];
			turkey_ssa_optimizer_touch_instruction(vm, function, bb, param_instruction.a);
			TurkeyInstruction &pa = basic_block.instructions[param_instruction.a];
			pa.return_type |= TT_Marked;
			param_instruction.return_type = pa.return_type;
		}
		break; }
	case turkey_ir_return_null:
		break;
	case turkey_ir_return: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		a.return_type |= TT_Marked;
		break; }
	case turkey_ir_push: {
		/* only should be called for exit pushes */
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		
		if(turkey_ssa_optimizer_is_constant(a)) {
			/* copy constant in place */
			instruction.instruction = a.instruction;
			instruction.large = a.large;
			instruction.return_type = a.return_type & TT_Mask;

			if(instruction.instruction == turkey_ir_string)
				turkey_gc_hold(vm, (TurkeyString *)instruction.large, TT_String);
		} else
			a.return_type |= TT_Marked;
		break; }
	case turkey_ir_get_type: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.a);
		TurkeyInstruction &a = basic_block.instructions[instruction.a];
		char type_a = a.return_type & TT_Mask;
		instruction.return_type = TT_String;

		switch(type_a) {
		case TT_Boolean:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_boolean;
			turkey_gc_hold(vm, vm->string_table.s_boolean, TT_String);
			break;
		case TT_Unsigned:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_unsigned;
			turkey_gc_hold(vm, vm->string_table.s_unsigned, TT_String);
			break;
		case TT_Signed:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_signed;
			turkey_gc_hold(vm, vm->string_table.s_signed, TT_String);
			break;
		case TT_Float:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_float;
			turkey_gc_hold(vm, vm->string_table.s_float, TT_String);
			break;
		case TT_Null:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_null;
			turkey_gc_hold(vm, vm->string_table.s_null, TT_String);
			break;
		case TT_Object:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_object;
			turkey_gc_hold(vm, vm->string_table.s_object, TT_String);
			break;
		case TT_Array:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_array;
			turkey_gc_hold(vm, vm->string_table.s_array, TT_String);
			break;
		case TT_Buffer:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_buffer;
			turkey_gc_hold(vm, vm->string_table.s_buffer, TT_String);
			break;
		case TT_FunctionPointer:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_function;
			turkey_gc_hold(vm, vm->string_table.s_function, TT_String);
			break;
		case TT_String:
			instruction.instruction = TT_String;
			instruction.large = (size_t)vm->string_table.s_string;
			turkey_gc_hold(vm, vm->string_table.s_string, TT_String);
			break;
		default:
		case TT_Unknown:
			break;
		}

		break; }
	case turkey_ir_jump:
		break;
	case turkey_ir_jump_if_true: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		b.return_type |= TT_Marked;
		break; }
	case turkey_ir_jump_if_false: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		b.return_type |= TT_Marked;
		break; }
	case turkey_ir_jump_if_null: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		b.return_type |= TT_Marked;
		break; }
	case turkey_ir_jump_if_not_null: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		b.return_type |= TT_Marked;
		break; }
	case turkey_ir_require: {
		turkey_ssa_optimizer_touch_instruction(vm, function, bb, instruction.b);
		TurkeyInstruction &b = basic_block.instructions[instruction.b];
		b.return_type |= TT_Marked;
		break; }
	}
}

void turkey_ssa_optimizer_optimize_function(TurkeyVM *vm, TurkeyFunction *function) {
	/* step 1: walk the bb, marking anything that is a root */
	for(unsigned bb = 0; bb < function->basic_blocks_count; bb++) {
		TurkeyBasicBlock &basic_block = function->basic_blocks[bb];

		for(unsigned inst = 0; inst < basic_block.instructions_count; inst++) {
			/* is not marked? */
			TurkeyInstruction &instruction = basic_block.instructions[inst];
			if((instruction.return_type & TT_Marked) == 0) {
				/* is a root instruction? */
				switch(instruction.instruction) {
				case turkey_ir_store_closure:
				case turkey_ir_save_element:
				case turkey_ir_delete_element:
				case turkey_ir_store_buffer_unsigned_8:
				case turkey_ir_store_buffer_unsigned_16:
				case turkey_ir_store_buffer_unsigned_32:
				case turkey_ir_store_buffer_unsigned_64:
				case turkey_ir_store_buffer_signed_8:
				case turkey_ir_store_buffer_signed_16:
				case turkey_ir_store_buffer_signed_32:
				case turkey_ir_store_buffer_signed_64:
				case turkey_ir_store_buffer_float_32:
				case turkey_ir_store_buffer_float_64:
				case turkey_ir_call_function:
				case turkey_ir_call_function_no_return:
				case turkey_ir_return_null:
				case turkey_ir_return:
				case turkey_ir_jump_if_true:
				case turkey_ir_jump_if_false:
				case turkey_ir_jump_if_null:
				case turkey_ir_jump_if_not_null:
					turkey_ssa_optimizer_touch_instruction(vm, function, bb, inst);
					instruction.return_type |= TT_Marked;
					break;
				}
			}
		}
	}

	/* step 2: propagate constants between basic blocks */

	/* step 3: scan roots again, now that constants are propagated between basic blocks */

	/* step 4: remove anything not touched */
}
#else
void turkey_ssa_optimizer_optimize_function(TurkeyVM *vm, TurkeyFunction *function) {
}
#endif
