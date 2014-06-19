#include "turkey.h"
#ifdef DEBUG
#include <stdio.h> /* for assert */
#endif

#define IS_GC_OBJECT(var) (var.type > TT_Null)

void turkey_gc_init(TurkeyVM *vm) {
	TurkeyGarbageCollector &collector = vm->garbage_collector;
#ifdef DEBUG
	collector.items_on_hold = 0;
#endif

	collector.arrays = 0;
	collector.buffers = 0;
	collector.function_pointers = 0;
	collector.objects = 0;
	collector.strings = 0;
	collector.closures = 0;

	collector.items = 0;
}

void turkey_gc_cleanup(TurkeyVM *vm) {
	TurkeyGarbageCollector &collector = vm->garbage_collector;
	TurkeyGarbageCollectedObject *iterator, *next;

#ifdef DEBUG
	assert(collector.items_on_hold == 0);
#endif

	/* delete everything */
	#define delete_everything(_IT_, _CAST_, _HANDLER_,_PARAM_) iterator = _IT_; \
		while(iterator != 0) { \
			next = iterator->gc_next; \
			_HANDLER_(_PARAM_,(_CAST_ *)iterator); \
			iterator = next; \
		}

	delete_everything(collector.arrays, TurkeyArray, turkey_array_delete, vm);
	delete_everything(collector.buffers, TurkeyBuffer, turkey_buffer_delete, vm);
	delete_everything(collector.function_pointers, TurkeyFunctionPointer, turkey_functionpointer_delete, vm);
	delete_everything(collector.objects, TurkeyObject, turkey_object_delete, vm);
	delete_everything(collector.closures, TurkeyClosure, turkey_closure_delete, vm);
	#undef delete_everything
		
	/* don't collect strings as we expect the string table to clean up its own business */
}

void turkey_gc_register_string(TurkeyGarbageCollector &collector, TurkeyString *string) {
	string->hold = 0;
	string->marked = false;
	string->gc_prev = 0;
	if(collector.strings != 0)
		collector.strings->gc_prev = string;
	string->gc_next = collector.strings;
	collector.strings = string;
	collector.items++;
}

void turkey_gc_register_buffer(TurkeyGarbageCollector &collector, TurkeyBuffer *buffer) {
	buffer->hold = 0;
	buffer->marked = false;
	buffer->gc_prev = 0;
	if(collector.buffers != 0)
		collector.buffers->gc_prev = buffer;
	buffer->gc_next = collector.buffers;
	collector.buffers = buffer;
	collector.items++;
}

void turkey_gc_register_array(TurkeyGarbageCollector &collector, TurkeyArray *arr) {
	arr->hold = 0;
	arr->marked = false;
	arr->gc_prev = 0;
	if(collector.arrays != 0)
		collector.arrays->gc_prev = arr;
	arr->gc_next = collector.arrays;
	collector.arrays = arr;
	collector.items++;
}

void turkey_gc_register_object(TurkeyGarbageCollector &collector, TurkeyObject *object) {
	object->hold = 0;
	object->marked = false;
	object->gc_prev = 0;
	if(collector.objects != 0)
		collector.objects->gc_prev = object;
	object->gc_next = collector.objects;
	collector.objects = object;
	collector.items++;
}

void turkey_gc_register_function_pointer(TurkeyGarbageCollector &collector, TurkeyFunctionPointer *function_pointer) {
	function_pointer->hold = 0;
	function_pointer->marked = false;
	function_pointer->gc_prev = 0;
	if(collector.function_pointers != 0)
		collector.function_pointers->gc_prev = function_pointer;
	function_pointer->gc_next = collector.function_pointers;
	collector.function_pointers = function_pointer;
	collector.items++;
}

void turkey_gc_register_closure(TurkeyGarbageCollector &collector, TurkeyClosure *closure) {
	closure->hold = 0;
	closure->marked = false;
	closure->gc_prev = 0;
	if(collector.closures != 0)
		collector.closures->gc_prev = closure;
	closure->gc_next = collector.closures;
	collector.closures = closure;
	collector.items++;
}

/* helpers while the GC is marking */
void turkey_gc_mark_closure(TurkeyVM *vm, TurkeyClosure *closure);

void turkey_gc_mark_variable(TurkeyVM *vm, TurkeyVariable &var) {
	/* caller has already done the IS_GC_OBJECT check, ignore TT_Closure because they can't be variables */

	switch(var.type) {
	case TT_Array: {
		TurkeyArray *arr = var.array;
		for(unsigned int i = 0; i < arr->length; i++) {
			if(IS_GC_OBJECT(arr->elements[i])) {
				if(!arr->elements[i].object->marked)
					turkey_gc_mark_variable(vm, arr->elements[i]);
			}
		}
		break; }
	case TT_Buffer: {
		TurkeyBuffer *buffer = var.buffer;
		buffer->marked = true;
		break; }
	case TT_FunctionPointer: {
		TurkeyFunctionPointer *func = var.function;
		func->marked = true;

		if(!func->is_native && func->managed.closure && !func->managed.closure->marked)
			turkey_gc_mark_closure(vm, func->managed.closure);

		break; }
	case TT_Object: {
		TurkeyObject *obj = var.object;

		/* scan every item in the object */
		for(unsigned int i = 0; i < obj->size; i++) {
			TurkeyObjectProperty *prop = obj->properties[i];
			while(prop != 0) {
				if(!prop->key->marked)
					prop->key->marked = true;

				if(IS_GC_OBJECT(prop->value)) {
					if(!prop->value.object->marked)
						turkey_gc_mark_variable(vm, prop->value);
				}

				prop = prop->next;
			}
		}
		break; }
	case TT_String: {
		TurkeyString *string = var.string;
		string->marked = true;
		break; }
	}
}

void turkey_gc_mark_stack(TurkeyVM *vm, TurkeyStack<TurkeyVariable> &stack) {
	for(unsigned int i = 0; i < stack.position; i++) {
		if(IS_GC_OBJECT(stack.variables[i])) {
			if(!stack.variables[i].object->marked)
				turkey_gc_mark_variable(vm, stack.variables[i]);
		}
	}
}

void turkey_gc_mark_closure(TurkeyVM *vm, TurkeyClosure *closure) {
	closure->marked = true;

	for(unsigned int i = 0; i < closure->count; i++) {
		if(IS_GC_OBJECT(closure->variables[i])) {
			if(!closure->variables[i].object->marked)
				turkey_gc_mark_variable(vm, closure->variables[i]);
		}
	}

	if(closure->parent && !closure->parent->marked)
		turkey_gc_mark_closure(vm, closure->parent);
}

void turkey_gc_mark_loaded_modules(TurkeyVM *vm, TurkeyLoadedModule *module) {
	while(module != 0) {
		if(IS_GC_OBJECT(module->ReturnVariable)) {
			if(!module->ReturnVariable.object->marked)
				turkey_gc_mark_variable(vm, module->ReturnVariable);
		}
		module = module->Next;
	}
}

void turkey_gc_collect(TurkeyVM *vm) {
	TurkeyGarbageCollector &collector = vm->garbage_collector;
	TurkeyGarbageCollectedObject *iterator, *next;

	/* unmarked everything except those held by native code*/
	/*#define mark_array(_IT_) iterator = _IT_; \
		while(iterator != 0) { \
			iterator->marked = false; \
			iterator = iterator->gc_next; \
		}

	mark_array(collector.arrays)
	mark_array(collector.buffers)
	mark_array(collector.function_pointers)
	mark_array(collector.objects)
	mark_array(collector.strings)

	#undef mark_array*/

	/* work up through the stacks and mark everything reachable */
	// turkey_gc_mark_stack(vm, vm->parameter_stack);
	turkey_gc_mark_stack(vm, vm->variable_stack);
	// turkey_gc_mark_stack(vm, vm->local_stack);

	/* work through the interpreter stack marking the closures */
	TurkeyInterpreterState *interpreterState = vm->interpreter_state;
	while(interpreterState != 0) {
		TurkeyClosure *closure = interpreterState->closure;
		if(!closure->marked)
			turkey_gc_mark_closure(vm, closure);
		interpreterState = interpreterState->parent;
	}

	/* work through the loaded modules */
	turkey_gc_mark_loaded_modules(vm, vm->loaded_modules.external_modules);
	turkey_gc_mark_loaded_modules(vm, vm->loaded_modules.internal_modules);

	/* cleanup everything unmarked */
	#define clean_up(_IT_, _CAST_, _HANDLER_,_PARAM_) iterator = _IT_; \
		while(iterator != 0) { \
			next = iterator->gc_next; \
			if(iterator->marked) \
				iterator->marked = false; \
			else { \
				if(iterator->gc_next != 0) \
					iterator->gc_next->gc_prev = iterator->gc_prev; \
				if(iterator->gc_prev != 0) \
					iterator->gc_prev->gc_next = iterator->gc_next; \
				else \
					_IT_ = (_CAST_ *)iterator->gc_next; \
				collector.items--; \
				_HANDLER_(_PARAM_,(_CAST_ *)iterator); \
			} \
			iterator = next; \
		}

	clean_up(collector.arrays, TurkeyArray, turkey_array_delete, vm);
	clean_up(collector.buffers, TurkeyBuffer, turkey_buffer_delete, vm);
	clean_up(collector.function_pointers, TurkeyFunctionPointer, turkey_functionpointer_delete, vm);
	clean_up(collector.objects, TurkeyObject, turkey_object_delete, vm);
	clean_up(collector.strings, TurkeyString, turkey_stringtable_removestring, vm);
	clean_up(collector.closures, TurkeyClosure, turkey_closure_delete, vm);
	#undef mark_array
}

/* Places a hold on an object - when saving references in native code - so garbage collector doesn't clean them up */
void turkey_gc_hold(TurkeyVM *vm, TurkeyVariable &variable) {
	
	/* make sure it's an allocated object */
	if(!IS_GC_OBJECT(variable))
		return;

	TurkeyGarbageCollectedObject *obj = (TurkeyGarbageCollectedObject *)variable.array;

	turkey_gc_hold(vm, obj, variable.type);
}

void turkey_gc_hold(TurkeyVM *vm, TurkeyGarbageCollectedObject *obj, TurkeyType type) {
	if(obj->hold == 0) {
		/* not yet held, so we need to remove it from the gc list */
		if(obj->gc_next != 0)
			obj->gc_next->gc_prev = obj->gc_prev;

		if(obj->gc_prev != 0)
			obj->gc_prev->gc_next = obj->gc_next;
		else {
			TurkeyGarbageCollector &collector = vm->garbage_collector;
			
			switch(type) {
			case TT_Array:
				collector.arrays = (TurkeyArray *)obj->gc_next;
				break;
			case TT_Buffer:
				collector.buffers = (TurkeyBuffer *)obj->gc_next;
				break;
			case TT_FunctionPointer:
				collector.function_pointers = (TurkeyFunctionPointer *)obj->gc_next;
				break;
			case TT_Object:
				collector.objects = (TurkeyObject *)obj->gc_next;
				break;
			case TT_String:
				collector.strings = (TurkeyString *)obj->gc_next;
				break;
			case TT_Closure:
				collector.closures = (TurkeyClosure *)obj->gc_next;
				break;
			}

			obj->gc_prev = 0;
		}

		obj->hold = 1;
#ifdef DEBUG
		if(type != TT_String)
			vm->garbage_collector.items_on_hold++;
#endif
	} else
		obj->hold++;
}

void turkey_gc_unhold(TurkeyVM *vm, TurkeyVariable &variable) {
	/* make sure it's an allocated object */
	if(!IS_GC_OBJECT(variable))
		return;

	TurkeyGarbageCollectedObject *obj = (TurkeyGarbageCollectedObject *)variable.array;
	turkey_gc_unhold(vm, obj, variable.type);
}

void turkey_gc_unhold(TurkeyVM *vm, TurkeyGarbageCollectedObject *obj, TurkeyType type) {
	if(obj->hold == 1) {
		/* last thing holding the object is releasing it, add it back to the gc list */
		obj->gc_prev = 0;
		obj->hold = 0;

		TurkeyGarbageCollector &collector = vm->garbage_collector;

		switch(type) {
		case TT_Array:
			obj->gc_next = collector.arrays;
			if(collector.arrays != 0) collector.arrays->gc_prev = obj;
			collector.arrays = (TurkeyArray *)obj;
			break;
		case TT_Buffer:
			obj->gc_next = collector.buffers;
			if(collector.buffers != 0) collector.buffers->gc_prev = obj;
			collector.buffers = (TurkeyBuffer *)obj;
			break;
		case TT_FunctionPointer:
			obj->gc_next = collector.function_pointers;
			if(collector.function_pointers != 0) collector.function_pointers->gc_prev = obj;
			collector.function_pointers = (TurkeyFunctionPointer *)obj;
			break;
		case TT_Object:
			obj->gc_next = collector.objects;
			if(collector.objects != 0) collector.objects->gc_prev = obj;
			collector.objects = (TurkeyObject *)obj;
			break;
		case TT_String:
			obj->gc_next = collector.strings;
			if(collector.strings != 0) collector.strings->gc_prev = obj;
			collector.strings = (TurkeyString *)obj;
			break;
		case TT_Closure:
			obj->gc_next = collector.closures;
			if(collector.closures != 0) collector.closures->gc_prev = obj;
			collector.closures = (TurkeyClosure *)obj;
		}
#ifdef DEBUG
		if(type != TT_String)
			collector.items_on_hold--;
#endif
	} else
		obj->hold--;
}