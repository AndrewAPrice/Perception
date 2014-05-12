#include "turkey_internal.h"

void turkey_gc_init(TurkeyGarbageCollector &collector) {
	collector.arrays = 0;
	collector.buffers = 0;
	collector.function_pointers = 0;
	collector.objects = 0;
	collector.strings = 0;

	collector.items = 0;
}

void turkey_gc_cleanup(TurkeyVM *vm) {	
	TurkeyGarbageCollector &collector = vm->garbage_collector;
	TurkeyGarbageCollectedObject *iterator, *next;

	/* delete everything */
	#define delete_everything(_IT_, _CAST_, _HANDLER_,_PARAM_) iterator = _IT_; \
		while(iterator != 0) { \
			next = iterator->gc_next; \
			_HANDLER_(_PARAM_,(_CAST_ *)_IT_); \
			iterator = next; \
		}

	delete_everything(collector.arrays, TurkeyArray, turkey_array_delete, vm);
	delete_everything(collector.buffers, TurkeyBuffer, turkey_buffer_delete, vm);
	// todo - function_pointers
	// todo - objects
	#undef delete_everything
		
	/* don't collect strings as we expect the string table to clean up its own business */
}

void turkey_gc_register_string(TurkeyGarbageCollector &collector, TurkeyString *string) {
	string->hold = 0;
	string->gc_prev = 0;
	if(collector.strings != 0)
		collector.strings->gc_prev = string;
	string->gc_next = collector.strings;
	collector.strings = string;
	collector.items++;
	
}

void turkey_gc_register_buffer(TurkeyGarbageCollector &collector, TurkeyBuffer *buffer) {
	buffer->hold = 0;
	buffer->gc_prev = 0;
	if(collector.buffers != 0)
		collector.buffers->gc_prev = buffer;
	buffer->gc_next = collector.buffers;
	collector.buffers = buffer;
	collector.items++;
}

void turkey_gc_register_array(TurkeyGarbageCollector &collector, TurkeyArray *arr) {
	arr->hold = 0;
	arr->gc_prev = 0;
	if(collector.arrays != 0)
		collector.arrays->gc_prev = arr;
	arr->gc_next = collector.arrays;
	collector.arrays = arr;
	collector.items++;
}

void turkey_gc_register_object(TurkeyGarbageCollector &collector, TurkeyObject *object) {
	object->hold = 0;
	object->gc_prev = 0;
	if(collector.objects != 0)
		collector.objects->gc_prev = object;
	object->gc_next = collector.objects;
	collector.objects = object;
	collector.items++;
}

void turkey_gc_register_function_pointer(TurkeyGarbageCollector &collector, TurkeyFunctionPointer *function_pointer) {
	function_pointer->hold = 0;
	function_pointer->gc_prev = 0;
	if(collector.function_pointers != 0)
		collector.function_pointers->gc_prev = function_pointer;
	function_pointer->gc_next = collector.function_pointers;
	collector.function_pointers = function_pointer;
	collector.items++;
}

void turkey_gc_collect(TurkeyVM *vm) {
	TurkeyGarbageCollector &collector = vm->garbage_collector;
	TurkeyGarbageCollectedObject *iterator, *next;

	/* unmarked everything except those held by native code*/
	#define mark_array(_IT_) iterator = _IT_; \
		while(iterator != 0) { \
			iterator->marked = false; \
			iterator->gc_next; \
		}

	mark_array(collector.arrays)
	mark_array(collector.buffers)
	mark_array(collector.function_pointers)
	mark_array(collector.objects)
	mark_array(collector.strings)

	#undef mark_array

	/* work up the call stack and mark everything reachable */

	/* cleanup everything unmarked */
	#define clean_up(_IT_, _CAST_, _HANDLER_,_PARAM_) iterator = _IT_; \
		while(iterator != 0) { \
			next = iterator->gc_next; \
			if(!iterator->marked) { \
				if(iterator->gc_next != 0) \
					iterator->gc_next->gc_prev = iterator->gc_prev; \
				if(iterator->gc_prev != 0) \
					iterator->gc_prev->gc_next = iterator->gc_next; \
				else \
					_IT_ = (_CAST_ *)iterator->gc_next; \
				collector.items--; \
				_HANDLER_(_PARAM_,(_CAST_ *)_IT_); \
			} \
			iterator = next; \
		}

	clean_up(collector.arrays, TurkeyArray, turkey_array_delete, vm);
	clean_up(collector.buffers, TurkeyBuffer, turkey_buffer_delete, vm);
	// todo - buffers
	// todo - function_pointers
	// todo - objects
	clean_up(collector.strings, TurkeyString, turkey_stringtable_removestring, vm->string_table);
	#undef mark_array
}

/* Places a hold on an object - when saving references in native code - so garbage collector doesn't clean them up */
void turkey_gc_hold(TurkeyVM *vm, TurkeyVariable &variable) {
	
	/* make sure it's an allocated object */
	if(variable.type != TT_Array && variable.type != TT_Buffer && variable.type != TT_FunctionPointer
		&& variable.type != TT_Object && variable.type != TT_String)
		return;

	TurkeyGarbageCollectedObject *obj = (TurkeyGarbageCollectedObject *)variable.array;

	if(obj->hold == 0) {
		/* not yet held, so we need to remove it from the gc list */
		if(obj->gc_next != 0)
			obj->gc_next->gc_prev = obj->gc_prev;

		if(obj->gc_prev != 0)
			obj->gc_prev->gc_next = obj->gc_next;
		else {
			TurkeyGarbageCollector &collector = vm->garbage_collector;
			
			switch(variable.type) {
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
			}
		}

		obj->hold = 1;
	} else
		obj->hold++;
}

void turkey_gc_unhold(TurkeyVM *vm, TurkeyVariable &variable) {
	/* make sure it's an allocated object */
	if(variable.type != TT_Array && variable.type != TT_Buffer && variable.type != TT_FunctionPointer
		&& variable.type != TT_Object && variable.type != TT_String)
		return;

	TurkeyGarbageCollectedObject *obj = (TurkeyGarbageCollectedObject *)variable.array;
	if(obj->hold == 1) {
		/* last thing holding the object is releasing it, add it back to the gc list */
		obj->gc_prev = 0;

		TurkeyGarbageCollector &collector = vm->garbage_collector;

		switch(variable.type) {
		case TT_Array:
			obj->gc_next = collector.arrays;
			collector.arrays = (TurkeyArray *)obj;
			break;
		case TT_Buffer:
			obj->gc_next = collector.buffers;
			collector.buffers = (TurkeyBuffer *)obj;
			break;
		case TT_FunctionPointer:
			obj->gc_next = collector.function_pointers;
			collector.function_pointers = (TurkeyFunctionPointer *)obj;
			break;
		case TT_Object:
			obj->gc_next = collector.objects;
			collector.objects = (TurkeyObject *)obj;
			break;
		case TT_String:
			obj->gc_next = collector.strings;
			collector.strings = (TurkeyString *)obj;
			break;
		}
	} else
		obj->hold--;
}
