#include "turkey.h"
#include "hooks.h"


void turkey_module_init(TurkeyVM *vm) {
	vm->modules = 0;
	vm->loaded_modules.external_modules = 0;
	vm->loaded_modules.internal_modules = 0;
}

void turkey_module_cleanup(TurkeyVM *vm) {
	{
		TurkeyLoadedModule *current = vm->loaded_modules.external_modules;
		TurkeyLoadedModule *next;

		while(current != 0) {
			next = current->Next;
			turkey_gc_unhold(vm, current->Name, TT_String);
			turkey_gc_unhold(vm, current->ReturnVariable);

			turkey_free_memory(vm->tag, current, sizeof TurkeyLoadedModule);

			current = next;
		}

		current = vm->loaded_modules.internal_modules;

		while(current != 0) {
			next = current->Next;
			turkey_gc_unhold(vm, current->Name, TT_String);
			turkey_gc_unhold(vm, current->ReturnVariable);

			turkey_free_memory(vm->tag, current, sizeof TurkeyLoadedModule);

			current = next;
		}

		vm->loaded_modules.external_modules = 0;
		vm->loaded_modules.internal_modules = 0;
	}

	{
		TurkeyModule *current = vm->modules;
		TurkeyModule *next;
		while(current != 0) {
			next = current->next;
			if(current->code_block != 0)
				turkey_free_memory(vm->tag, current->code_block, current->code_block_size);

			if(current->functions != 0) {
				for(unsigned int i = 0; i < current->function_count; i++) {
					if(current->functions[i] != 0) {
						turkey_free_memory(vm->tag, current->functions[i], sizeof TurkeyFunction);
					}
				}

				turkey_free_memory(vm->tag, current->functions, sizeof(TurkeyFunction *) * current->function_count);
			}

			if(current->strings != 0) {
				for(unsigned int i = 0; i < current->string_count; i++) {
					if(current->strings[i] != 0) {
						turkey_gc_unhold(vm, current->strings[i], TT_String);
					}
				}
				
				turkey_free_memory(vm->tag, current->strings, sizeof (TurkeyString *) * current->string_count);
			}

			turkey_free_memory(vm->tag, current, sizeof TurkeyModule);
			current = next;
		}
	}
}

/* Loads a file and runs its main function - pushes it's exports object */
void turkey_require(TurkeyVM *vm, unsigned int index) {
	// copy it to the bottom
	turkey_grab(vm, index);
	turkey_require(vm);
}

void turkey_require(TurkeyVM *vm) {
	TurkeyVariable name;
	turkey_stack_pop(vm->variable_stack, name);

	TurkeyVariable ret;
	ret.type = TT_Null;

	TurkeyString *strName = turkey_to_string(vm, name);
	turkey_gc_hold(vm, strName, TT_String);
	// load it, pop it back on the stack
	if(strName->length > 0) {
		if(strName->string[0] == '.') {
			// local file
			TurkeyString *absPath = turkey_relative_to_absolute_path(vm, strName);
			turkey_gc_hold(vm, absPath, TT_String);

			// see if it's already loaded
			bool found = false;
			TurkeyLoadedModule *current = vm->loaded_modules.external_modules;
			while(current != 0) {
				if(current->Name == strName) {
					// return what's already loaded
					ret = current->ReturnVariable;
					found = true;
					turkey_gc_unhold(vm, absPath, TT_String); // release our hold on the string
					break;
				}
				current->Next;
			}

			// if it's not already loaded
			if(!found) {
				// add this module first before loading the file - this is important to prevent recursive loading
				TurkeyLoadedModule *module = (TurkeyLoadedModule *)turkey_allocate_memory(vm->tag, sizeof TurkeyLoadedModule);
				module->Name = absPath; // absPath is already being heald
				module->ReturnVariable = ret;
				module->Next = vm->loaded_modules.external_modules;
				vm->loaded_modules.external_modules = module;

				// load this module
				ret = turkey_module_load_file(vm, absPath);
				
				// register what it returns as a module
				turkey_gc_hold(vm, ret);
				module->ReturnVariable = ret;

			}
		} else {
			// an internal module
			
			TurkeyLoadedModule *current = vm->loaded_modules.internal_modules;
			while(current != 0) {
				if(current->Name == strName) {
					// found it!
					ret = current->ReturnVariable;
					break;
				}
				current = current->Next;
			}
			// fall through will return null
		}
	}

	turkey_gc_unhold(vm, strName, TT_String);
	turkey_stack_push(vm, vm->variable_stack, ret);
	
}

/* Registers an internal module. When Shovel/Turkey code calls require, it checks to see if there's an internal module to return first
   - if there is then 'obj' passed here is returned, otherwise it loads a physical file on disk. */
void turkey_register_module(TurkeyVM *vm, unsigned int ind_moduleName, unsigned int ind_obj) {
	TurkeyVariable name, obj;

	turkey_stack_get(vm->variable_stack, ind_moduleName, name);
	turkey_stack_get(vm->variable_stack, ind_obj, obj);

	TurkeyString *strName = turkey_to_string(vm, name);
	turkey_gc_hold(vm, strName, TT_String);
	turkey_gc_hold(vm, obj);

	// create a module
	TurkeyLoadedModule *module = (TurkeyLoadedModule *)turkey_allocate_memory(vm->tag, sizeof TurkeyLoadedModule);
	module->Name = strName;
	module->ReturnVariable = obj;
	module->Next = vm->loaded_modules.internal_modules;
	vm->loaded_modules.internal_modules = module;
}

void read_functions_from_file(TurkeyVM *vm, TurkeyModule *module,
	unsigned int function_header_start, unsigned int functions,
	unsigned int code_block_start, unsigned int code_block_length,
	void *file, size_t file_size) {
		unsigned int function_table_length = functions * 5 * 4;
		if(function_header_start + function_table_length > file_size ||
			code_block_start + code_block_length > file_size ||
			functions == 0
			) {
			// function header or code block cannot fit in the file!
			module->function_count = 0;
			module->functions = 0;
			module->code_block = 0;
			module->code_block_size = 0;
			return;
		}

		// allocate the code block
		module->code_block = turkey_allocate_memory(vm->tag, code_block_length);
		module->code_block_size = code_block_length;
		turkey_memory_copy(module->code_block, (void *)((size_t)file + code_block_start), code_block_length);

		// allocate function headers
		module->functions = (TurkeyFunction **)turkey_allocate_memory(vm->tag, sizeof(TurkeyFunction *) * functions);
		module->function_count = functions;

		unsigned int start_addr = 0;

		for(unsigned int i = 0; i < functions; i++) {
			unsigned int code_length = *(unsigned int *)((size_t)file + function_header_start + i * 5 * 4);
			// skip debug block
			unsigned int parameters = *(unsigned int *)((size_t)file + function_header_start + i * 5 * 4 + 8);
			unsigned int local_vars = *(unsigned int *)((size_t)file + function_header_start + i * 5 * 4 + 12);
			unsigned int closure_vars = *(unsigned int *)((size_t)file + function_header_start + i * 5 * 4 + 16);

			if(start_addr + code_length > code_block_length) {
				// cannot fit in the code block
				module->functions[i] = 0;
			} else {
				TurkeyFunction *function = (TurkeyFunction *)turkey_allocate_memory(vm->tag, sizeof TurkeyFunction);
				
				function->module = module;
				function->start = (void *)((size_t)module->code_block + start_addr);
				function->end = (void *)((size_t)module->code_block + start_addr + code_length);
				function->parameters = parameters;
				function->locals = local_vars;
				function->closures = closure_vars;
				module->functions[i] = function;
			}

			start_addr += code_length;
		}
}

void load_string_table_from_file(TurkeyVM *vm, TurkeyModule *module,
	unsigned int string_table_start, unsigned int string_table_entries,
	void *file, size_t file_size) {
		unsigned int string_table_length = string_table_entries * 4;
		if(string_table_start + string_table_length > file_size || string_table_entries == 0) {
			// string table cannot fit in the file!
			module->string_count = 0;
			module->strings = 0;
			return;
		}

		module->string_count = string_table_entries;	
		module->strings = (TurkeyString **) turkey_allocate_memory(vm->tag, sizeof (TurkeyString *) * string_table_entries);

		unsigned int str_start = string_table_start + string_table_length;
		// add each string
		for(unsigned int i = 0; i < string_table_entries; i++) {
			unsigned int str_len = *(unsigned int *)((size_t)file + string_table_start + i * 4);
			if(str_start > file_size || str_start + str_len > file_size) {
				module->strings[i] = 0; // no string here
			} else {
				TurkeyString *str = turkey_stringtable_newstring(vm, (char *)((size_t)file + str_start), str_len);
				turkey_gc_hold(vm, str, TT_String);
				module->strings[i] = str;
			}
			str_start += str_len;
		}
}

/* load a file and return the return value from the module's default function */
TurkeyVariable turkey_module_load_file(TurkeyVM *vm, TurkeyString *filepath) {
	TurkeyVariable ret;
	ret.type = TT_Null; // default return value if an error occurs

	// load the file
	size_t file_size;
	void *file = turkey_load_file(vm->tag, filepath, file_size);
	if(file == 0) return ret; // couldn't load file

	if(file_size < 26) { // not enough room for a header
		turkey_free_memory(vm->tag, file, file_size);
		return ret;
	}

	// read the magic key '12SHOVEL'
	if(*(unsigned int *)file != 0x48533231 || *(unsigned int *)((size_t)file + 4) != 0x4C45564F) {
		// magic key is bad
		turkey_free_memory(vm->tag, file, file_size);
		return ret;
	}

	if(*(unsigned short *)((size_t)file + 8) != 0) {
		// unknown version
		turkey_free_memory(vm->tag, file, file_size);
		return ret;
	}

	// create module in memory
	TurkeyModule *module = (TurkeyModule *)turkey_allocate_memory(vm->tag, sizeof TurkeyModule);
	module->next = 0;
	vm->modules = module;

	unsigned int function_header_start = 26;
	unsigned int functions = *(unsigned int *)((size_t)file + 10);
	unsigned int code_block_start = function_header_start + functions * 20;
	unsigned int code_block_length = *(unsigned int *)((size_t)file + 14);

	read_functions_from_file(vm, module, function_header_start, functions, code_block_start, code_block_length, file, file_size);

	unsigned int string_table_start = code_block_start + code_block_length;
	unsigned int string_table_entries = *(unsigned int *)((size_t)file + 18);

	load_string_table_from_file(vm, module, string_table_start, string_table_entries, file, file_size);

	// unload the file
	turkey_free_memory(vm->tag, file, file_size);

	// execute the first function
	if(module->function_count >= 1 && module->functions[0] != 0) {
		TurkeyFunctionPointer function_ptr;
		function_ptr.is_native = false;
		function_ptr.managed.function = module->functions[0];
		function_ptr.managed.closure = 0;

		ret = turkey_call_function(vm, &function_ptr, 0);
	}

	// return what the first function returns, null if anything bad happened
	return ret;
}