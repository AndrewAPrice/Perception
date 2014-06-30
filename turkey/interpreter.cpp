#include "hooks.h"
#include "turkey.h"
#include <varargs.h> /* needed for vaarg intristics */

void turkey_interpreter_init(TurkeyVM *vm) {
	vm->interpreter_state = 0;
}

void turkey_interpreter_cleanup(TurkeyVM *vm) {
	assert(vm->interpreter_state == 0); // still in the middle of vm execution
}

TurkeyVariable turkey_call_function(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, unsigned int argc) {
	if(argc > 16) argc = 16;

	if(funcptr->is_native) {
		/* native function call */
		TurkeyVariable var;
		
		/* this huge switch statement converts the stack to varargs - does c++ have a way of doing this dynamically? */
		switch(argc) {
		case 0:
			var = funcptr->native.function(vm, funcptr->native.closure, argc, 0);
			break;
		case 1: {
			uint64_t type;

			TurkeyVariable tmp;

			if(!vm->variable_stack.Pop(tmp)) tmp.type = TT_Null;
			type = tmp.type;

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp.unsigned_value);
			break; }
		case 2: {
			uint64_t type;

			TurkeyVariable tmp[2];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value);
			break; }
		case 3: {
			uint64_t type;

			TurkeyVariable tmp[3];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value);
			break; }
		case 4: {
			uint64_t type;

			TurkeyVariable tmp[4];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value);
			break; }
		case 5: {
			uint64_t type;

			TurkeyVariable tmp[5];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value);
			break; }
		case 6: {
			uint64_t type;

			TurkeyVariable tmp[6];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value);
			break; }
		case 7: {
			uint64_t type;

			TurkeyVariable tmp[7];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value);
			break; }
		case 8: {
			uint64_t type;

			TurkeyVariable tmp[8];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[7])) tmp[7].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24) | (tmp[7].type << 28);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value, tmp[7].unsigned_value);
			break; }
		case 9: {
			uint64_t type;

			TurkeyVariable tmp[9];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[7])) tmp[7].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[8])) tmp[8].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24) | (tmp[7].type << 28) | ((uint64_t)tmp[8].type << 32);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value, tmp[7].unsigned_value, tmp[8].unsigned_value);
			break; }
		case 10: {
			uint64_t type;

			TurkeyVariable tmp[10];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[7])) tmp[7].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[8])) tmp[8].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[9])) tmp[9].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24) | (tmp[7].type << 28) | ((uint64_t)tmp[8].type << 32) | ((uint64_t)tmp[9].type << 36);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value, tmp[7].unsigned_value, tmp[8].unsigned_value,
				tmp[9].unsigned_value);
			break; }
		case 11: {
			uint64_t type;

			TurkeyVariable tmp[11];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[7])) tmp[7].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[8])) tmp[8].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[9])) tmp[9].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[10])) tmp[10].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24) | (tmp[7].type << 28) | ((uint64_t)tmp[8].type << 32) | ((uint64_t)tmp[9].type << 36) | ((uint64_t)tmp[10].type << 40);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value, tmp[7].unsigned_value, tmp[8].unsigned_value,
				tmp[9].unsigned_value, tmp[10].unsigned_value);
			break; }
		case 12: {
			uint64_t type;

			TurkeyVariable tmp[12];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[7])) tmp[7].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[8])) tmp[8].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[9])) tmp[9].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[10])) tmp[10].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[11])) tmp[11].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24) | (tmp[7].type << 28) | ((uint64_t)tmp[8].type << 32) | ((uint64_t)tmp[9].type << 36) | ((uint64_t)tmp[10].type << 40) |
				((uint64_t)tmp[11].type << 44);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value, tmp[7].unsigned_value, tmp[8].unsigned_value,
				tmp[9].unsigned_value, tmp[10].unsigned_value, tmp[11].unsigned_value);
			break; }
		case 13: {
			uint64_t type;

			TurkeyVariable tmp[13];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[7])) tmp[7].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[8])) tmp[8].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[9])) tmp[9].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[10])) tmp[10].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[11])) tmp[11].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[12])) tmp[12].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24) | (tmp[7].type << 28) | ((uint64_t)tmp[8].type << 32) | ((uint64_t)tmp[9].type << 36) | ((uint64_t)tmp[10].type << 40) |
				((uint64_t)tmp[11].type << 44) | ((uint64_t)tmp[12].type << 48);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value, tmp[7].unsigned_value, tmp[8].unsigned_value,
				tmp[9].unsigned_value, tmp[10].unsigned_value, tmp[11].unsigned_value, tmp[12].unsigned_value);
			break; }
		case 14: {
			uint64_t type;

			TurkeyVariable tmp[14];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[7])) tmp[7].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[8])) tmp[8].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[9])) tmp[9].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[10])) tmp[10].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[11])) tmp[11].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[12])) tmp[12].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[13])) tmp[13].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24) | (tmp[7].type << 28) | ((uint64_t)tmp[8].type << 32) | ((uint64_t)tmp[9].type << 36) | ((uint64_t)tmp[10].type << 40) |
				((uint64_t)tmp[11].type << 44) | ((uint64_t)tmp[12].type << 48) | ((uint64_t)tmp[13].type << 52);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value, tmp[7].unsigned_value, tmp[8].unsigned_value,
				tmp[9].unsigned_value, tmp[10].unsigned_value, tmp[11].unsigned_value, tmp[12].unsigned_value, tmp[13].unsigned_value);
			break; }
		case 15: {
			uint64_t type;

			TurkeyVariable tmp[15];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[7])) tmp[7].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[8])) tmp[8].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[9])) tmp[9].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[10])) tmp[10].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[11])) tmp[11].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[12])) tmp[12].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[13])) tmp[13].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[14])) tmp[14].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24) | (tmp[7].type << 28) | ((uint64_t)tmp[8].type << 32) | ((uint64_t)tmp[9].type << 36) | ((uint64_t)tmp[10].type << 40) |
				((uint64_t)tmp[11].type << 44) | ((uint64_t)tmp[12].type << 48) | ((uint64_t)tmp[13].type << 52) | ((uint64_t)tmp[14].type << 56);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value, tmp[7].unsigned_value, tmp[8].unsigned_value,
				tmp[9].unsigned_value, tmp[10].unsigned_value, tmp[11].unsigned_value, tmp[12].unsigned_value, tmp[13].unsigned_value, tmp[14].unsigned_value);
			break; }
		case 16: {
			uint64_t type;

			TurkeyVariable tmp[16];

			if(!vm->variable_stack.Pop(tmp[0])) tmp[0].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[1])) tmp[1].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[2])) tmp[2].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[3])) tmp[3].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[4])) tmp[4].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[5])) tmp[5].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[6])) tmp[6].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[7])) tmp[7].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[8])) tmp[8].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[9])) tmp[9].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[10])) tmp[10].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[11])) tmp[11].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[12])) tmp[12].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[13])) tmp[13].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[14])) tmp[14].type = TT_Null;
			if(!vm->variable_stack.Pop(tmp[15])) tmp[15].type = TT_Null;
			type = tmp[0].type | (tmp[1].type << 4) | (tmp[2].type << 8) | (tmp[3].type << 12) | (tmp[4].type << 16) | (tmp[5].type << 20) |
				(tmp[6].type << 24) | (tmp[7].type << 28) | ((uint64_t)tmp[8].type << 32) | ((uint64_t)tmp[9].type << 36) | ((uint64_t)tmp[10].type << 40) |
				((uint64_t)tmp[11].type << 44) | ((uint64_t)tmp[12].type << 48) | ((uint64_t)tmp[13].type << 52) | ((uint64_t)tmp[14].type << 56) |
				((uint64_t)tmp[15].type << 60);

			var = funcptr->native.function(vm, funcptr->native.closure, argc, type, tmp[0].unsigned_value, tmp[1].unsigned_value, tmp[2].unsigned_value,
				tmp[3].unsigned_value, tmp[4].unsigned_value, tmp[5].unsigned_value, tmp[6].unsigned_value, tmp[7].unsigned_value, tmp[8].unsigned_value,
				tmp[9].unsigned_value, tmp[10].unsigned_value, tmp[11].unsigned_value, tmp[12].unsigned_value, tmp[13].unsigned_value, tmp[14].unsigned_value,
				tmp[15].unsigned_value);
			break; }
		}
		
		return var;
	}

	// call managed function
	TurkeyFunction *func = funcptr->managed.function;

	TurkeyInterpreterState state;
	state.parent = vm->interpreter_state;
	state.function = func;
	vm->interpreter_state = &state;

	// set up closures
	state.closure = funcptr->managed.closure;
	if(func->closures > 0) {
		state.closure = turkey_closure_create(vm, funcptr->managed.closure, func->closures);
	} else
		state.closure = funcptr->managed.closure;

	state.code_start = state.code_ptr = (size_t)func->start;
	state.code_end = (size_t)func->end;
	state.executing = true;

	TurkeyVariable nullvar;
	nullvar.type = TT_Null;
	
	// do we have enough parameters on our stack?
	{
		unsigned int stack_size = vm->variable_stack.position - vm->variable_stack.top;
		while(stack_size < argc) {
			// not enough parameters on the local stack, push nulls
			vm->variable_stack.Push(nullvar);
			stack_size++;
		}
	}
	
	// set up local stack
	unsigned int caller_variable_stack_top = vm->variable_stack.top;
	vm->variable_stack.top = vm->variable_stack.position - argc;

	if(argc > func->parameters) {
		// too many parameters
		vm->variable_stack.position -= argc - func->parameters;
	} else {
		while(argc < func->parameters) {
			// too few parameters
			vm->variable_stack.Push(nullvar);
			argc++;
		}
	}
	
	// start executing
	while(state.executing && state.code_start <= state.code_ptr && state.code_ptr < state.code_end) {
		// call this instruction
		unsigned char bytecode = *(unsigned char *)state.code_ptr++;
		turkey_interpreter_operations[bytecode](vm);
	}

	// get the return value
	TurkeyVariable ret;
	if(!vm->variable_stack.Pop(ret)) ret.type = TT_Null;

	// return to the parent state
	vm->variable_stack.position = vm->variable_stack.top;
	vm->variable_stack.top = caller_variable_stack_top;

	vm->interpreter_state = state.parent;

	return ret;
}

void turkey_call_function_no_return(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, unsigned int argc) {
	turkey_call_function(vm, funcptr, argc);
}
