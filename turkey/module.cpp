#include "turkey_internal.h"
#include "hooks.h"


void turkey_module_init(TurkeyVM *vm) {
	vm->loaded_modules.external_modules = 0;
	vm->loaded_modules.internal_modules = 0;
}

void turkey_module_cleanup(TurkeyVM *vm) {
	TurkeyLoadedModule *current = vm->loaded_modules.external_modules;
	TurkeyLoadedModule *next;

	while(current != 0) {
		next = current->Next;
		turkey_gc_unhold(vm, current->Name, TT_String);
		turkey_gc_unhold(vm, current->ReturnVariable);

		turkey_free_memory(current);

		current = next;
	}

	current = vm->loaded_modules.internal_modules;

	while(current != 0) {
		next = current->Next;
		turkey_gc_unhold(vm, current->Name, TT_String);
		turkey_gc_unhold(vm, current->ReturnVariable);

		turkey_free_memory(current);

		current = next;
	}
	
	vm->loaded_modules.external_modules = 0;
	vm->loaded_modules.internal_modules = 0;
}

/* Loads a file and runs its main function - pushes it's exports object */
void turkey_require(TurkeyVM *vm, unsigned int index) {
	// copy it to the bottom
	turkey_grab(vm, index);
	turkey_require(vm);
}

void turkey_require(TurkeyVM *vm) {
	TurkeyVariable name;
	turkey_stack_pop(vm->local_stack, name);

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
				// load this module
				ret = turkey_module_load_file(vm, absPath);
				
				// register what it returns as a module
				// absPath is already being heald
				turkey_gc_hold(vm, ret);

				TurkeyLoadedModule *module = (TurkeyLoadedModule *)turkey_allocate_memory(sizeof TurkeyLoadedModule);
				module->Name = strName;
				module->ReturnVariable = ret;
				module->Next = vm->loaded_modules.external_modules;
				vm->loaded_modules.external_modules = module;
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
	turkey_stack_push(vm->local_stack, ret);
	
}

/* Registers an internal module. When Shovel/Turkey code calls require, it checks to see if there's an internal module to return first
   - if there is then 'obj' passed here is returned, otherwise it loads a physical file on disk. */
void turkey_register_module(TurkeyVM *vm, unsigned int ind_moduleName, unsigned int ind_obj) {
	TurkeyVariable name, obj;

	turkey_stack_get(vm->local_stack, ind_moduleName, name);
	turkey_stack_get(vm->local_stack, ind_obj, obj);

	TurkeyString *strName = turkey_to_string(vm, name);
	turkey_gc_hold(vm, strName, TT_String);
	turkey_gc_hold(vm, obj);

	// create a module
	TurkeyLoadedModule *module = (TurkeyLoadedModule *)turkey_allocate_memory(sizeof TurkeyLoadedModule);
	module->Name = strName;
	module->ReturnVariable = obj;
	module->Next = vm->loaded_modules.internal_modules;
	vm->loaded_modules.internal_modules = module;
}

/* load a file and return the return value from the module's default function */
TurkeyVariable turkey_module_load_file(TurkeyVM *vm, TurkeyString *file) {
	TurkeyVariable ret;
	ret.type = TT_Null; // default return value if an error occurs

	return ret;
}