#include "turkey.h"

/* define PRINT_SSA to print out the SSA form */
#define PRINT_SSA

#ifdef PRINT_SSA
#include <stdio.h>
#endif

struct ssa_bytecode_markers {
	unsigned int basic_block;
	bool is_opcode;
	int stack_size;
};

void turkey_ssa_compile_function(TurkeyVM *vm, TurkeyFunction *function) {
	/* STEP 1: scan for basic blocks
	   scan the bytecode for markers where basic blocks begin, and keep a track of the stack size at each bytecode */
	unsigned char *bytecode = (unsigned char *)function->start;

	int stacksize = function->parameters;

	size_t bytecodeLength = (size_t)function->end - (size_t)function->start;

	if(bytecodeLength == 0)
		return;

	bool next_bytecode_is_basic_block = false;

	size_t bytecode_pos = 0;

	size_t basic_block_count = 1;

	ssa_bytecode_markers *bytecode_markers = (ssa_bytecode_markers *)turkey_allocate_memory(vm->tag, bytecodeLength * sizeof(ssa_bytecode_markers));
	turkey_memory_clear(bytecode_markers, bytecodeLength * sizeof(ssa_bytecode_markers));

	bytecode_markers[0].basic_block = 1;

	#define exit_error() { \
		turkey_free_memory(vm->tag, bytecode_markers, bytecodeLength * sizeof(ssa_bytecode_markers)); \
		return; \
	}

	while(bytecode < function->end) {
		if(next_bytecode_is_basic_block) {
			/* don't overwrite it if it's already set - e.g. jumping ahead */
			bytecode_markers[bytecode_pos].basic_block = 1;
			next_bytecode_is_basic_block = false;
			basic_block_count++;
		}
		bytecode_markers[bytecode_pos].is_opcode = true;
		bytecode_markers[bytecode_pos].stack_size = stacksize;

		switch(*bytecode) {
		case turkey_instruction_add: case turkey_instruction_subtract: case turkey_instruction_divide:
		case turkey_instruction_multiply: case turkey_instruction_modulo: case turkey_instruction_xor:
		case turkey_instruction_and: case turkey_instruction_or: case turkey_instruction_shift_left:
		case turkey_instruction_shift_right: case turkey_instruction_rotate_left: case turkey_instruction_rotate_right:
		case turkey_instruction_equals: case turkey_instruction_not_equals: case turkey_instruction_less_than:
		case turkey_instruction_greater_than: case turkey_instruction_less_than_or_equals: case turkey_instruction_greater_than_or_equals:
		case turkey_instruction_pop: case turkey_instruction_load_buffer_unsigned_8:
		case turkey_instruction_load_buffer_unsigned_16: case turkey_instruction_load_buffer_unsigned_32: case turkey_instruction_load_buffer_unsigned_64:
		case turkey_instruction_load_buffer_signed_8: case turkey_instruction_load_buffer_signed_16: case turkey_instruction_load_buffer_signed_32:
		case turkey_instruction_load_buffer_signed_64: case turkey_instruction_load_buffer_float_32: case turkey_instruction_load_buffer_float_64:
			stacksize--;
			break;

		case turkey_instruction_load_element:
			next_bytecode_is_basic_block = true;
			stacksize--;
			break;

		case turkey_instruction_increment: case turkey_instruction_decrement: case turkey_instruction_not:
		case turkey_instruction_is_null: case turkey_instruction_is_not_null: case turkey_instruction_is_true:
		case turkey_instruction_is_false: case turkey_instruction_new_array: case turkey_instruction_new_buffer:
		case turkey_instruction_to_integer: case turkey_instruction_to_unsigned_integer: case turkey_instruction_to_float:
		case turkey_instruction_get_type: case turkey_instruction_to_string:
		case turkey_instruction_invert:		
		default:
			break;

		case turkey_instruction_require:
			next_bytecode_is_basic_block = true;
			break;

		case turkey_instruction_save_element: case turkey_instruction_store_buffer_unsigned_8: case turkey_instruction_store_buffer_unsigned_16:
		case turkey_instruction_store_buffer_unsigned_32: case turkey_instruction_store_buffer_unsigned_64: case turkey_instruction_store_buffer_signed_8:
		case turkey_instruction_store_buffer_signed_16: case turkey_instruction_store_buffer_signed_32: case turkey_instruction_store_buffer_signed_64:
		case turkey_instruction_store_buffer_float_32: case turkey_instruction_store_buffer_float_64:
			stacksize -= 3;
			break;
		
		case turkey_instruction_new_object: case turkey_instruction_push_true: case turkey_instruction_push_false:
		case turkey_instruction_push_null:
			stacksize++;
			break;

		case turkey_instruction_delete_element:
			stacksize -= 2;
			break;

		case turkey_instruction_pop_many:
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
			unsigned char pop_amount = *bytecode;
			stacksize -= pop_amount;
			}
			break;
		case turkey_instruction_grab_8: case turkey_instruction_load_closure_8: case turkey_instruction_push_integer_8:
		case turkey_instruction_push_unsigned_integer_8: case turkey_instruction_push_string_8:
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;

			stacksize++;
			break;
		case turkey_instruction_grab_16: case turkey_instruction_load_closure_16: case turkey_instruction_push_integer_16:
		case turkey_instruction_push_unsigned_integer_16: case turkey_instruction_push_string_16:
			if(bytecode + 2 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;

			stacksize++;
			break;
		case turkey_instruction_grab_32: case turkey_instruction_load_closure_32: case turkey_instruction_push_integer_32:
		case turkey_instruction_push_unsigned_integer_32: case turkey_instruction_push_string_32: case turkey_instruction_push_function:
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;

			stacksize++;
			break;
		case turkey_instruction_push_integer_64: case turkey_instruction_push_unsigned_integer_64: case turkey_instruction_push_float:
			if(bytecode + 8 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;

			stacksize++;
			break;
		case turkey_instruction_store_8: case turkey_instruction_store_closure_8:
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;

			stacksize--;
			break;
		case turkey_instruction_store_16: case turkey_instruction_store_closure_16:
			if(bytecode + 2 >= function->end)
				exit_error();

			
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;

			stacksize--;
			break;
		case turkey_instruction_store_32: case turkey_instruction_store_closure_32:
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;

			stacksize--;
			break;
		case turkey_instruction_push_many_nulls:
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
			unsigned char null_amount = *bytecode;
			stacksize += null_amount;
			}
			break;
		case turkey_instruction_swap_8:
			if(bytecode + 2 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			
			break;
		case turkey_instruction_swap_16:
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			break;
		case turkey_instruction_swap_32:
			if(bytecode + 8 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			break;
		
		case turkey_instruction_call_function_8: case turkey_instruction_call_procedure_8: case turkey_instruction_call_function_no_return_8:
			if(bytecode + 1 >= function->end)
				exit_error();
			
			if(*bytecode != turkey_instruction_call_function_no_return_8) {
				next_bytecode_is_basic_block = true;
				stacksize++;
			}

			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
				unsigned char args = *bytecode;
				stacksize -= (int)args - 1;
			}
			break;

		case turkey_instruction_call_function_16: case turkey_instruction_call_procedure_16: case turkey_instruction_call_function_no_return_16:
			if(bytecode + 2 >= function->end)
				exit_error();

			if(*bytecode != turkey_instruction_call_function_no_return_16) {
				next_bytecode_is_basic_block = true;
				stacksize++;
			}
			
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
				unsigned short args = *(unsigned short *)bytecode;
				stacksize -= (int)args + 1;
			}
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			
			break;
		case turkey_instruction_return_null:
			next_bytecode_is_basic_block = true;
			break;
		case turkey_instruction_return:
			stacksize -= 1;
			next_bytecode_is_basic_block = true;
			break;
		case turkey_instruction_jump_8:
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
				unsigned char pos = *bytecode;
				if((size_t)pos >= (size_t)function->end) exit_error();
				if(!bytecode_markers[pos].basic_block) {
					bytecode_markers[pos].basic_block = 1;
					basic_block_count++;
				}
			}

			next_bytecode_is_basic_block = true;
			break;
		case turkey_instruction_jump_16:
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
				unsigned short pos = *(unsigned short *)bytecode;
				if((size_t)pos >= (size_t)function->end) exit_error();
				if(!bytecode_markers[pos].basic_block) {
					bytecode_markers[pos].basic_block = 1;
					basic_block_count++;
				}
			}
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			
			next_bytecode_is_basic_block = true;
			break;
		case turkey_instruction_jump_32:
			if(bytecode + 4 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
				unsigned int pos = *(unsigned int*)bytecode;
				if((size_t)pos >= (size_t)function->end) exit_error();
				if(!bytecode_markers[pos].basic_block) {
					bytecode_markers[pos].basic_block = 1;
					basic_block_count++;
				}
			}
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			
			next_bytecode_is_basic_block = true;
			break;
		case turkey_instruction_jump_if_true_8:
		case turkey_instruction_jump_if_false_8:
		case turkey_instruction_jump_if_null_8:
		case turkey_instruction_jump_if_not_null_8:
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
				unsigned char pos = *bytecode;
				if((size_t)pos >= (size_t)function->end) exit_error();
				if(!bytecode_markers[pos].basic_block) {
					bytecode_markers[pos].basic_block = 1;
					basic_block_count++;
				}
			}
			next_bytecode_is_basic_block = true;
			stacksize--;
			break;
		case turkey_instruction_jump_if_true_16:
		case turkey_instruction_jump_if_false_16:
		case turkey_instruction_jump_if_null_16:
		case turkey_instruction_jump_if_not_null_16:
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
				unsigned short pos = *(unsigned short *)bytecode;
				if((size_t)pos >= (size_t)function->end) exit_error();
				if(!bytecode_markers[pos].basic_block) {
					bytecode_markers[pos].basic_block = 1;
					basic_block_count++;
				}
			}
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			next_bytecode_is_basic_block = true;
			stacksize--;
			break;
		case turkey_instruction_jump_if_true_32:
		case turkey_instruction_jump_if_false_32:
		case turkey_instruction_jump_if_null_32:
		case turkey_instruction_jump_if_not_null_32:
			if(bytecode + 4 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			{
				unsigned int pos = *(unsigned int*)bytecode;
				if((size_t)pos >= (size_t)function->end) exit_error();
				if(!bytecode_markers[pos].basic_block) {
					bytecode_markers[pos].basic_block = 1;
					basic_block_count++;
				}
			}
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			bytecode++; bytecode_pos++; bytecode_markers[bytecode_pos].is_opcode = false;
			next_bytecode_is_basic_block = true;
			stacksize--;

			break;
		}
		bytecode++;
		bytecode_pos++;
	}
	
	/* STEP 2: loop over the basic blocks to give each an ID */
	bytecode = (unsigned char *)function->start;
	bytecode_pos = 0;
	unsigned int basic_block_no = 1;

	while(bytecode < function->end) {
		if(bytecode_markers[bytecode_pos].basic_block) {
			if(!bytecode_markers[bytecode_pos].is_opcode)
				exit_error();

			bytecode_markers[bytecode_pos].basic_block = basic_block_no;

			basic_block_no++;
			}
		
		bytecode++;
		bytecode_pos++;
	}

	unsigned int total_basic_blocks = basic_block_no - 1;
	
#undef exit_error

	/* STEP 3: construct SSA instructions for each basic block */
#define exit_error() { \
		for(unsigned int i = 0; i < total_basic_blocks; i++) { \
			if(function->basic_blocks[i].instructions_count != 0) \
				turkey_free_memory(vm->tag, function->basic_blocks[i].instructions, (sizeof TurkeyInstruction) * \
					function->basic_blocks[i].instructions_count); \
		} \
		turkey_free_memory(vm->tag, function->basic_blocks, (sizeof TurkeyBasicBlock) * total_basic_blocks); \
		turkey_free_memory(vm->tag, bytecode_markers, bytecodeLength * sizeof(ssa_bytecode_markers)); \
		return; \
	}

	/* allocate and empty the basic blocks */
	function->basic_blocks_count = total_basic_blocks;
	function->basic_blocks = (TurkeyBasicBlock *)turkey_allocate_memory(vm->tag, (sizeof TurkeyBasicBlock) * total_basic_blocks);
	for(unsigned int i = 0; i < total_basic_blocks; i++) {
		function->basic_blocks->instructions_count = 0;
	}

	bytecode = (unsigned char *)function->start;
	bytecode_pos = 0;
	basic_block_no = 0;

	TurkeyStack<unsigned int> stack(vm->tag);
	TurkeyStack<TurkeyInstruction> instructions(vm->tag);

	unsigned int codePos;

	while(bytecode < function->end) {
		if(bytecode_markers[bytecode_pos].basic_block) {
			if(!bytecode_markers[bytecode_pos].is_opcode)
				exit_error();

			/* push whatever is left onto the stack so it is sent to the next function */
			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			if(basic_block_no != 0) {
				/* ending another basic block, save their instructions */
				function->basic_blocks[basic_block_no - 1].instructions_count = instructions.position;
				if(instructions.position == 0)
					function->basic_blocks[basic_block_no - 1].instructions = 0;
				else {
					function->basic_blocks[basic_block_no - 1].instructions = (TurkeyInstruction *)turkey_allocate_memory(vm->tag,
						(sizeof TurkeyInstruction) * instructions.position); 
					turkey_memory_copy(function->basic_blocks[basic_block_no - 1].instructions,
						instructions.variables, (sizeof TurkeyInstruction) * instructions.position);
				}
			}


			/* start a new basic block */

			stack.Clear();
			instructions.Clear();

			codePos = 0;

			function->basic_blocks[basic_block_no].stack_entry = bytecode_markers[bytecode_pos].stack_size;
			for(unsigned int i = 0; i < function->basic_blocks[basic_block_no].stack_entry; i++) {
				stack.Push(codePos);
				TurkeyInstruction inst; inst.instruction = turkey_ir_parameter;
				inst.a = function->basic_blocks[basic_block_no].stack_entry - (unsigned int)i - 1;

				instructions.Push(inst);
				codePos++;
			}
			basic_block_no++;
		}

		assert(basic_block_no > 0);

		/* iterate over the instructions and generate SSA IR for each */
		switch(*bytecode) {
		case turkey_instruction_add: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_add;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_subtract: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_subtract;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_divide: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_divide;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_multiply: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_multiply;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_modulo: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_modulo;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_increment: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_increment;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_decrement: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_decrement;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_xor: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_xor;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_and: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_and;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_or: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_or;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_not: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_not;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_shift_left: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_shift_left;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_shift_right:  {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_shift_right;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_rotate_left: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_rotate_left;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_rotate_right: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_rotate_right;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_is_null: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_is_null;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_is_not_null: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_is_not_null;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_equals: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_equals;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_not_equals: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_not_equals;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_less_than: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_less_than;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_greater_than: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_greater_than;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_less_than_or_equals: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_less_than_or_equals;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_greater_than_or_equals: {
			unsigned int a, b;
			if(!stack.Pop(b)) exit_error();
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_greater_than_or_equals;
			inst.a = a, inst.b = b;

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_is_true: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_is_true;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_is_false: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_is_false;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_pop: {
			stack.PopNoReturn();
			break;
		}
		case turkey_instruction_pop_many: {
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			{
				unsigned char pop_amount = *bytecode;
				while(pop_amount > 0) {
					stack.PopNoReturn();
					pop_amount--;
				}
			}
			break; }
		case turkey_instruction_grab_8: {
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			{
				unsigned char index = *bytecode;
				unsigned int i;
				if(!stack.Get((unsigned int)index, i)) exit_error();
				stack.Push(i);
			}
			break; }
		case turkey_instruction_grab_16: {
			if(bytecode + 2 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			{
				unsigned short index = *(unsigned short*)bytecode;
				unsigned int i;
				if(!stack.Get((unsigned int)index, i)) exit_error();
				stack.Push(i);
			}
			bytecode++; bytecode_pos++;

			stacksize++;
			break; }
		case turkey_instruction_grab_32: {
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			{
				unsigned int index = *(unsigned int*)bytecode;
				unsigned int i;
				if(!stack.Get(index, i)) exit_error();
				stack.Push(i);
			}
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			stacksize++;
			break; }
		
		case turkey_instruction_push_many_nulls: {
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			{
				unsigned char null_amount = *bytecode;
				if(null_amount > 0) {
					TurkeyInstruction inst; inst.instruction = turkey_ir_null;
					instructions.Push(inst);
					while(null_amount > 0) {
						stack.Push(codePos);
						null_amount--;
					}

					codePos++;
				}
			}
			break; }
		case turkey_instruction_store_8: {
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned char pos = *bytecode;
			unsigned int a;
			if(!stack.Pop(a)) exit_error();
			stack.Set((unsigned int)pos, a);

			break; }
		case turkey_instruction_store_16: {
			if(bytecode + 2 >= function->end)
				exit_error();

			
			bytecode++; bytecode_pos++;
			unsigned short pos = *(unsigned short *)bytecode;
			unsigned int a;
			if(!stack.Pop(a)) exit_error();
			stack.Set((unsigned int)pos, a);

			bytecode++; bytecode_pos++;

			stacksize--;
			break; }
		case turkey_instruction_store_32: {
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int pos = *(unsigned int *)bytecode;
			unsigned int a;
			if(!stack.Pop(a)) exit_error();
			stack.Set(pos, a);

			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			stacksize--;
			break; }
		case turkey_instruction_swap_8: {
			if(bytecode + 2 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int posa = (unsigned int)*bytecode;
			bytecode++; bytecode_pos++;
			unsigned int posb = (unsigned int)*bytecode;

			unsigned int a, b;
			if(!stack.Get(posa, a)) exit_error();
			if(!stack.Get(posb, b)) exit_error();
			stack.Set(posa, b);
			stack.Set(posb, a);

			break;
		}
		case turkey_instruction_swap_16: {
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int posa = (unsigned int)*(unsigned short*)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			unsigned int posb = (unsigned int)*(unsigned short*)bytecode;
			bytecode++; bytecode_pos++;
			
			unsigned int a, b;
			if(!stack.Get(posa, a)) exit_error();
			if(!stack.Get(posb, b)) exit_error();
			stack.Set(posa, b);
			stack.Set(posb, a);

			break; }
		case turkey_instruction_swap_32: {
			if(bytecode + 8 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int posa = *(unsigned int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			unsigned int posb = *(unsigned int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			
			unsigned int a, b;
			if(!stack.Get(posa, a)) exit_error();
			if(!stack.Get(posb, b)) exit_error();
			stack.Set(posa, b);
			stack.Set(posb, a);

			break; }
		case turkey_instruction_load_closure_8: {
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;

			unsigned int closure = (unsigned int)*bytecode;

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_closure;
			inst.a = closure;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
						
			break; }
		case turkey_instruction_load_closure_16: {
			if(bytecode + 2 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int closure = (unsigned int)*(unsigned short*)bytecode;
			bytecode++; bytecode_pos++;

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_closure;
			inst.a = closure;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_closure_32: {
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int closure = *(unsigned int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_closure;
			inst.a = closure;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_store_closure_8: {
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;

			unsigned int closure = (unsigned int)*bytecode;
			
			unsigned b;
			if(!stack.Pop(b)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_closure;
			inst.a = closure;
			inst.b = b;
			instructions.Push(inst);
			codePos++;
						
			break; }
		case turkey_instruction_store_closure_16: {
			if(bytecode + 2 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int closure = (unsigned int)*(unsigned short *)bytecode;
			bytecode++; bytecode_pos++;

			unsigned b;
			if(!stack.Pop(b)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_closure;
			inst.a = closure;
			inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_store_closure_32: {
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int closure = *(unsigned int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			unsigned b;
			if(!stack.Pop(b)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_closure;
			inst.a = closure;
			inst.b = b;
			instructions.Push(inst);
			codePos++;

			break; }
		case turkey_instruction_new_array: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_new_array;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_element: {
			unsigned int key;
			unsigned int arr;
			if(!stack.Pop(arr)) exit_error();
			if(!stack.Pop(key)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_element;
			inst.a = key; inst.b = arr;
			instructions.Push(inst);
			stack.Push(codePos);

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			codePos++;
			break; }
		case turkey_instruction_save_element: {
			unsigned int value;
			unsigned int key;
			unsigned int arr;
			if(!stack.Pop(arr)) exit_error();
			if(!stack.Pop(key)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;
			inst.instruction = turkey_ir_save_element;
			inst.a = key; inst.b = arr;

			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_new_object: {
			TurkeyInstruction inst; inst.instruction = turkey_ir_new_object;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_delete_element: {
			unsigned int key, object;
			if(!stack.Pop(object)) exit_error();
			if(!stack.Pop(key)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_delete_element;
			inst.a = key; inst.b = object;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_new_buffer: {
			unsigned int size;
			if(!stack.Pop(size)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_new_array;
			inst.a = size;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_buffer_unsigned_8: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_unsigned_8;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_buffer_unsigned_16: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_unsigned_16;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_buffer_unsigned_32: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_unsigned_32;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_buffer_unsigned_64: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_unsigned_64;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_store_buffer_unsigned_8: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_unsigned_8;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }		
		case turkey_instruction_store_buffer_unsigned_16: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_unsigned_16;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }	
		case turkey_instruction_store_buffer_unsigned_32: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_unsigned_32;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_store_buffer_unsigned_64: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_unsigned_64;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }	
		case turkey_instruction_load_buffer_signed_8: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_signed_8;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_buffer_signed_16: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_signed_16;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_buffer_signed_32: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_signed_32;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_buffer_signed_64: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_signed_64;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_store_buffer_signed_8: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_signed_8;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }	
		case turkey_instruction_store_buffer_signed_16: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_signed_16;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_store_buffer_signed_32: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_signed_32;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_store_buffer_signed_64: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_signed_64;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_load_buffer_float_32: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_float_32;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_load_buffer_float_64: {
			unsigned int address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_load_buffer_float_64;
			inst.a = address; inst.b = buffer;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_store_buffer_float_32: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_float_32;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_store_buffer_float_64: {
			unsigned int value, address, buffer;
			if(!stack.Pop(buffer)) exit_error();
			if(!stack.Pop(address)) exit_error();
			if(!stack.Pop(value)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_push;
			inst.a = value;
			instructions.Push(inst);
			codePos++;

			inst.instruction = turkey_ir_store_buffer_float_64;
			inst.a = address; inst.b = value;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_push_integer_8: {
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			long long int value = (long long int)*(signed char *)bytecode;

			TurkeyInstruction inst; inst.instruction = turkey_ir_signed_integer;
			*(long long int *)&inst.large = value;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_integer_16: {
			if(bytecode + 2 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			long long int value = (long long int)*(signed short *)bytecode;
			bytecode++; bytecode_pos++;

			TurkeyInstruction inst; inst.instruction = turkey_ir_signed_integer;
			*(long long int *)&inst.large = value;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_integer_32: {
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			long long int value = (long long int)*(signed int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			
			TurkeyInstruction inst; inst.instruction = turkey_ir_signed_integer;
			*(long long int *)&inst.large = value;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_integer_64: {
			if(bytecode + 8 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			long long int value = *(long long int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			TurkeyInstruction inst; inst.instruction = turkey_ir_signed_integer;
			*(long long int *)&inst.large = value;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_to_integer: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_to_signed_integer;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		
		case turkey_instruction_push_unsigned_integer_8: {
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned long long int value = (unsigned long long int)*(unsigned char *)bytecode;

			TurkeyInstruction inst; inst.instruction = turkey_ir_unsigned_integer;
			inst.large = value;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_unsigned_integer_16: {
			if(bytecode + 2 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned long long int value = (unsigned long long int)*(unsigned short *)bytecode;
			bytecode++; bytecode_pos++;

			TurkeyInstruction inst; inst.instruction = turkey_ir_unsigned_integer;
			inst.large = value;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_unsigned_integer_32: {
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned long long int value = (unsigned long long int)*(unsigned int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			
			TurkeyInstruction inst; inst.instruction = turkey_ir_unsigned_integer;
			inst.large = value;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_unsigned_integer_64: {
			if(bytecode + 8 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned long long int value = *(unsigned long long int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			TurkeyInstruction inst; inst.instruction = turkey_ir_unsigned_integer;
			inst.large = value;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_to_unsigned_integer: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_to_unsigned_integer;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_float: {
			if(bytecode + 8 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			double value = *(double *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			TurkeyInstruction inst; inst.instruction = turkey_ir_float;
			*(double *)&inst.large = value;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_to_float: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_to_float;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_true: {
			TurkeyInstruction inst; inst.instruction = turkey_ir_true;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_false: {
			TurkeyInstruction inst; inst.instruction = turkey_ir_false;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_null: {
			TurkeyInstruction inst; inst.instruction = turkey_ir_null;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_string_8: {
			if(bytecode + 1 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int string_index = (unsigned int)*bytecode;

			if(string_index >= function->module->string_count) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_string;
			TurkeyString *str = function->module->strings[string_index];
			inst.large = (unsigned long long int)str;
			if(inst.large == 0) exit_error();

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_string_16: {
			if(bytecode + 2 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int string_index = (unsigned int)*(unsigned short *)bytecode;
			bytecode++; bytecode_pos++;
			
			if(string_index >= function->module->string_count) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_string;
			TurkeyString *str = function->module->strings[string_index];
			inst.large = (unsigned long long int)str;
			if(inst.large == 0) exit_error();

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_push_string_32: {
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int string_index = *(unsigned int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			if(string_index >= function->module->string_count) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_string;
			TurkeyString *str = function->module->strings[string_index];
			inst.large = (unsigned long long int)str;
			if(inst.large == 0) exit_error();

			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			break; }
		case turkey_instruction_push_function: {
			if(bytecode + 4 >= function->end)
				exit_error();

			bytecode++; bytecode_pos++;
			unsigned int func = *(unsigned int *)bytecode;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			if(func >= function->module->function_count) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_function;
			inst.large = (unsigned long long int)function->module->functions[func];
			if(inst.large == 0) exit_error();
			
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_call_function_8: {
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int args = (unsigned int)*bytecode;

			unsigned int func;
			if(!stack.Pop(func)) exit_error();

			for(unsigned int i = 0; i < args; i++) {
				unsigned int a;
				if(!stack.Pop(a)) exit_error();
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = a;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_call_function;
			inst.a = args;
			inst.b = func;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			break; }
		case turkey_instruction_call_function_16: {
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int args = (unsigned int)*(unsigned short *)bytecode;

			bytecode++; bytecode_pos++;
			
			unsigned int func;
			if(!stack.Pop(func)) exit_error();

			for(unsigned int i = 0; i < args; i++) {
				unsigned int a;
				if(!stack.Pop(a)) exit_error();
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = a;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_call_function;
			inst.a = args;
			inst.b = func;
			instructions.Push(inst);
			stack.Push(codePos);

			codePos++;

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			break; }

		case turkey_instruction_call_function_no_return_8: {
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int args = (unsigned int)*bytecode;

			unsigned int func;
			if(!stack.Pop(func)) exit_error();

			for(unsigned int i = 0; i < args; i++) {
				unsigned int a;
				if(!stack.Pop(a)) exit_error();
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = a;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_call_function;
			inst.a = args;
			inst.b = func;
			instructions.Push(inst);

			codePos++;

			break; }
		case turkey_instruction_call_function_no_return_16: {
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int args = (unsigned int)*(unsigned short *)bytecode;
			
			bytecode++; bytecode_pos++;

			unsigned int func;
			if(!stack.Pop(func)) exit_error();

			for(unsigned int i = 0; i < args; i++) {
				unsigned int a;
				if(!stack.Pop(a)) exit_error();
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = a;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_call_function;
			inst.a = args;
			inst.b = func;
			instructions.Push(inst);

			codePos++;

			break; }
		case turkey_instruction_return_null: {
			TurkeyInstruction inst; inst.instruction = turkey_ir_return_null;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_return: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();
			TurkeyInstruction inst; inst.instruction = turkey_ir_return;
			inst.a = a;
			instructions.Push(inst);
			codePos++;
			break; }
		
		case turkey_instruction_get_type: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();
			TurkeyInstruction inst; inst.instruction = turkey_ir_get_type;
			inst.a = a;
			instructions.Push(inst);
			codePos++;
			break;}
		
		case turkey_instruction_jump_8: {
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned char pos = *bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump;
			inst.a = bb;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_16: {
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned short pos = *(unsigned short *)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump;
			inst.a = bb;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_32: {
			if(bytecode + 4 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int pos = *(unsigned int*)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump;
			inst.a = bb;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_true_8: {
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned char pos = *bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_true;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_true_16: {
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned short pos = *(unsigned short *)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_true;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_true_32: {
			if(bytecode + 4 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int pos = *(unsigned int*)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_true;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_false_8: {
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned char pos = *bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_false;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_false_16: {
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned short pos = *(unsigned short *)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_false;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_false_32: {
			if(bytecode + 4 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int pos = *(unsigned int*)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_false;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_null_8: {
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned char pos = *bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_null;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_null_16: {
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned short pos = *(unsigned short *)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_null;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_null_32: {
			if(bytecode + 4 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int pos = *(unsigned int*)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_null;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_not_null_8: {
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned char pos = *bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_not_null;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_not_null_16: {
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned short pos = *(unsigned short *)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_not_null;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_jump_if_not_null_32: {
			if(bytecode + 4 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int pos = *(unsigned int*)bytecode;
			unsigned int bb = bytecode_markers[pos].basic_block - 1;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;
			bytecode++; bytecode_pos++;

			unsigned int b;
			if(!stack.Pop(b)) exit_error();

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_jump_if_not_null;
			inst.a = bb; inst.b = b;
			instructions.Push(inst);
			codePos++;
			break; }
		case turkey_instruction_require: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_require;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}
			break; }
		case turkey_instruction_to_string: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_to_string;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_invert: {
			unsigned int a;
			if(!stack.Pop(a)) exit_error();

			TurkeyInstruction inst; inst.instruction = turkey_ir_invert;
			inst.a = a;
			instructions.Push(inst);
			stack.Push(codePos);
			codePos++;
			break; }
		case turkey_instruction_call_procedure_8: {
			if(bytecode + 1 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int args = (unsigned int)*bytecode;

			unsigned int func;
			if(!stack.Pop(func)) exit_error();

			for(unsigned int i = 0; i < args; i++) {
				unsigned int a;
				if(!stack.Pop(a)) exit_error();
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = a;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_call_pure_function;
			inst.a = args;
			inst.b = func;
			instructions.Push(inst);
			stack.Push(codePos);

			codePos++;

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			break; }
		case turkey_instruction_call_procedure_16: {
			if(bytecode + 2 >= function->end)
				exit_error();
			
			bytecode++; bytecode_pos++;
			unsigned int args = (unsigned int)*(unsigned short *)bytecode;
			
			bytecode++; bytecode_pos++;

			unsigned int func;
			if(!stack.Pop(func)) exit_error();

			for(unsigned int i = 0; i < args; i++) {
				unsigned int a;
				if(!stack.Pop(a)) exit_error();
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = a;
				instructions.Push(inst);
				codePos++;
			}

			TurkeyInstruction inst; inst.instruction = turkey_ir_call_pure_function;
			inst.a = args;
			inst.b = func;
			instructions.Push(inst);
			stack.Push(codePos);

			codePos++;

			unsigned int param;
			while(stack.Pop(param)) {
				TurkeyInstruction inst; inst.instruction = turkey_ir_push;
				inst.a = param;
				instructions.Push(inst);
				codePos++;
			}

			break; }
		default:
#ifdef PRINT_SSA
			printf(" Unknown opcode %u\n", (unsigned int)*bytecode);
#endif
			exit_error();
			break;
		}

		bytecode++;
		bytecode_pos++;
	}

	/* ending another basic block, save their instructions */
	function->basic_blocks[basic_block_no - 1].instructions_count = instructions.position;
	if(instructions.position == 0)
		function->basic_blocks[basic_block_no - 1].instructions = 0;
	else {
		function->basic_blocks[basic_block_no - 1].instructions = (TurkeyInstruction *)turkey_allocate_memory(vm->tag,
			(sizeof TurkeyInstruction) * instructions.position); 
		turkey_memory_copy(function->basic_blocks[basic_block_no - 1].instructions,
			instructions.variables, (sizeof TurkeyInstruction) * instructions.position);
	}

#undef exit_error
	/* free bytecode */
	turkey_free_memory(vm->tag, bytecode_markers, bytecodeLength * sizeof(ssa_bytecode_markers));

	/* set all types to unknown, optimizer and jit need this */
	for(unsigned int i = 0; i < function->basic_blocks_count; i++) {
		for(unsigned int j = 0; j < function->basic_blocks[i].instructions_count; j++)
			function->basic_blocks[i].instructions[j].return_type = TT_Unknown;
	}

	/* optimize ssa */
#ifdef SSA_OPTIMIZER
	turkey_ssa_optimizer_optimize_function(vm, function);
#else
#endif
	
	/* print ssa*/
	turkey_ssa_printer_print_function(vm, function);
}
