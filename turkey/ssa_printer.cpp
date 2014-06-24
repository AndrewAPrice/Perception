/* prints SSA form, compile with PRINT_SSA to output the internal SSA form after loading a module, there is a preprocessor
   tag in turkey.h to uncomment for this */
#include "turkey.h"

#ifdef PRINT_SSA
#include <stdio.h> /* for printf */

void turkey_ssa_printer_print_function(TurkeyVM *vm, TurkeyFunction *function) {
	printf("Printing SSA for function:\n");
	for(unsigned int i = 0; i < function->basic_blocks_count; i++) {
		const TurkeyBasicBlock &bb = function->basic_blocks[i];
		unsigned int entry_points = bb.entry_point_count;
		if(i == 0)
			entry_points++;

		printf(" Basic block (BB%i, entry points: %i", i, entry_points);
		if(entry_points > 0) {
			printf(" - ");
			bool first;
			if(i == 0) {
				printf("function");
				first = false;
			} else
				first = true;

			for(unsigned int j = 0; j < bb.entry_point_count; j++) {
				if(first) first = false;
				else printf(", ");

				printf("BB%i", bb.entry_points[j]);
			}
		}

		printf("):\n", i, entry_points);

		for(unsigned int j = 0; j < bb.instructions_count; j++) {
			const TurkeyInstruction &inst = bb.instructions[j];

			const char *return_type;

			switch(inst.return_type) {
			case TT_Boolean:
				return_type = " Boolean";
				break;
			case TT_Unsigned:
				return_type = " Unsigned";
				break;
			case TT_Signed:
				return_type = " Signed";
				break;
			case TT_Float:
				return_type = " Float";
				break;
			case TT_Null:
				return_type = " Null";
				break;
			case TT_Object:
				return_type = " Object";
				break;
			case TT_Buffer:
				return_type = " Buffer";
				break;
			case TT_FunctionPointer:
				return_type = " Function";
				break;
			case TT_String:
				return_type = " String";
				break;
			default:
				return_type = "";
				break;
			}

			printf("  %i%s: ", j, return_type);

			switch(inst.instruction) {
				case turkey_ir_add:
					printf("Add [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_subtract:
					printf("Subtract [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_divide:
					printf("Divide [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_multiply:
					printf("Multiply [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_modulo:
					printf("Modulo [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_invert:
					printf("Invert [%u]\n", inst.a); break;
				case turkey_ir_increment:
					printf("Increment [%u]\n", inst.a); break;
				case turkey_ir_decrement:
					printf("Decrement [%u]\n", inst.a); break;
				case turkey_ir_xor:
					printf("Xor [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_and:
					printf("And [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_or:
					printf("Or [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_not:
					printf("Not [%u]\n", inst.a); break;
				case turkey_ir_shift_left:
					printf("ShiftLeft [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_shift_right:
					printf("ShiftRight [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_rotate_left:
					printf("RotateLeft [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_rotate_right:
					printf("RotateRight [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_is_null:
					printf("IsNull [%u]\n", inst.a); break;
				case turkey_ir_is_not_null:
					printf("IsNotNull [%u]\n", inst.a); break;
				case turkey_ir_equals:
					printf("Equals [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_not_equals:
					printf("NotEquals [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_less_than:
					printf("LessThan [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_greater_than:
					printf("GreaterThan [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_less_than_or_equals:
					printf("LessThanOrEquals [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_greater_than_or_equals:
					printf("GreaterThanOrEquals [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_is_true:
					printf("IsTrue [%u]\n", inst.a); break;
				case turkey_ir_is_false:
					printf("IsFalse [%u]\n", inst.a); break;
				case turkey_ir_parameter:
					printf("Parameter %u\n", inst.a); break;
				case turkey_ir_load_closure:
					printf("LoadClosure %u\n", inst.a); break;
				case turkey_ir_store_closure:
					printf("StoreClosure %u, [%u]\n", inst.a, inst.b); break;
				case turkey_ir_new_array:
					printf("NewArray [%u]\n", inst.a); break;
				case turkey_ir_load_element:
					printf("LoadElement [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_save_element:
					printf("SaveElement [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_new_object:
					printf("NewObject\n"); break;
				case turkey_ir_delete_element:
					printf("DeleteElement [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_new_buffer:
					printf("NewBuffer [%u]\n", inst.a); break;
				case turkey_ir_load_buffer_unsigned_8:
					printf("LoadBufferUnsigned8 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_load_buffer_unsigned_16:
					printf("LoadBufferUnsigned16 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_load_buffer_unsigned_32:
					printf("LoadBufferUnsigned32 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_load_buffer_unsigned_64:
					printf("LoadBufferUnsigned64 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_unsigned_8:
					printf("StoreBufferUnsigned8 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_unsigned_16:
					printf("StoreBufferUnsigned16 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_unsigned_32:
					printf("StoreBufferUnsigned32 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_unsigned_64:
					printf("StoreBufferUnsigned64 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_load_buffer_signed_8:
					printf("LoadBufferSigned8 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_load_buffer_signed_16:
					printf("LoadBufferSigned16 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_load_buffer_signed_32:
					printf("LoadBufferSigned32 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_load_buffer_signed_64:
					printf("LoadBufferSigned64 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_signed_8:
					printf("StoreBufferSigned8 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_signed_16:
					printf("StoreBufferSigned16 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_signed_32:
					printf("StoreBufferSigned32 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_signed_64:
					printf("StoreBufferSigned64 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_load_buffer_float_32:
					printf("LoadBufferFloat32 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_load_buffer_float_64:
					printf("LoadBufferFloat64 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_float_32:
					printf("StoreBufferFloat32 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_store_buffer_float_64:
					printf("StoreBufferFloat64 [%u] [%u]\n", inst.a, inst.b); break;
				case turkey_ir_signed_integer:
					printf("SignedInteger %lli\n", *(signed long long int *)&inst.large); break;
				case turkey_ir_to_signed_integer:
					printf("ToSignedInteger [%u]\n", inst.a); break;
				case turkey_ir_unsigned_integer:
					printf("UnsignedInteger %llu\n", inst.large); break;
				case turkey_ir_to_unsigned_integer:
					printf("ToUnsignedInteger [%u]\n", inst.a); break;
				case turkey_ir_float:
					printf(" Float %f\n", *(double *)&inst.large); break;
				case turkey_ir_to_float:
					printf("ToFloat [%u]\n", inst.a); break;
				case turkey_ir_true:
					printf("True\n"); break;
				case turkey_ir_false:
					printf("False\n"); break;
				case turkey_ir_null:
					printf("Null\n"); break;
				case turkey_ir_string:
					printf("String *%llu (\"%.*s\")\n", inst.large, ((TurkeyString *)inst.large)->length, ((TurkeyString *)inst.large)->string); break;
				case turkey_ir_to_string:
					printf("ToString [%u]\n", inst.a); break;
				case turkey_ir_function:
					printf("Function *%llu\n", inst.large); break;
				case turkey_ir_call_function:
					printf("CallFunction %u [%u]\n", inst.a, inst.b); break;
				case turkey_ir_call_function_no_return:
					printf("CallFunctionNoReturn %u [%u]\n", inst.a, inst.b); break;
				case turkey_ir_call_pure_function:
					printf("CallPureFunction %u [%u]\n", inst.a, inst.b); break;
				case turkey_ir_return_null:
					printf("ReturnNull\n"); break;
				case turkey_ir_return:
					printf("Return [%u]\n", inst.a); break;
				case turkey_ir_push:
					printf("Push [%u]\n", inst.a); break;
				case turkey_ir_get_type:
					printf("GetType [%u]\n", inst.a); break;
				case turkey_ir_jump:
					printf("Jump BB%u\n", inst.a); break;
				case turkey_ir_jump_if_true:
					printf("JumpIfTrue BB%u, [%u]\n", inst.a, inst.b); break;
				case turkey_ir_jump_if_false:
					printf("JumpIfFalse BB%u, [%u]\n", inst.a, inst.b); break;
				case turkey_ir_jump_if_null:
					printf("JumpIfNull BB%u, [%u]\n", inst.a, inst.b); break;
				case turkey_ir_jump_if_not_null:
					printf("JumpIfNotNull BB%u, [%u]\n", inst.a, inst.b); break;
				case turkey_ir_require:
					printf("Require [%u]\n", inst.a); break;
				default:
					printf("Unknown\n"); break;
			}
		}
	}
}
#else
void turkey_ssa_printer_print_function(TurkeyVM *vm, TurkeyFunction *function) {}
#endif
