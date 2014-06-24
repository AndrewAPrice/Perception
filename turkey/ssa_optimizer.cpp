#include "turkey.h"

/* optimizes SSA form, compile with OPTIMIZE_SSA to optimize SSA form after loading a module - there is a preprocessor line in
   ssa_optimizer.cpp for this */

#ifdef OPTIMIZE_SSA
void turkey_ssa_optimizer_mark_instruction(TurkeyVM *vm, TurkeyFunction *function, unsigned int bb, unsigned int inst) {
	/* marks an instruction as being needed, and finds any dependencies */
}

void turkey_ssa_optimizer_optimize_function(TurkeyVM *vm, TurkeyFunction *function) {
	/* walk the bb, marking anything that is a root */
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
					turkey_ssa_optimizer_mark_instruction(vm, function, bb, inst);
						break;
				}
			}
		}
	}
}
#else
void turkey_ssa_optimizer_optimize_function(TurkeyVM *vm, TurkeyFunction *function) {
}
#endif
