#include "turkey.h"
#include "hooks.h"

TurkeyObject *turkey_object_new(TurkeyVM *vm) {
	TurkeyObject *object = (TurkeyObject *)turkey_allocate_memory(vm->tag, sizeof(TurkeyObject));
	object->count = 0;
	object->size = 2;
	object->properties = (TurkeyObjectProperty **)turkey_allocate_memory(vm->tag, (sizeof (TurkeyObjectProperty *)) * object->size);

	/* clear hash */
	for(unsigned int i = 0; i < object->size; i++)
		object->properties[i] = 0;

	
	/* register with the gc */
	turkey_gc_register_object(vm->garbage_collector, object);

	return object;
}

void turkey_object_delete(TurkeyVM *vm, TurkeyObject *object) {
	/* release each property */
	for(unsigned int i = 0; i < object->size; i++) {
		TurkeyObjectProperty *prop = object->properties[i];
		while(prop != 0) {
			TurkeyObjectProperty *next = prop->next;
			turkey_free_memory(vm->tag, prop, sizeof(TurkeyObjectProperty));
			prop = next;
		}
	}

	turkey_free_memory(vm->tag, object->properties, (sizeof (TurkeyObjectProperty *)) * object->size);
	turkey_free_memory(vm->tag, object, sizeof(TurkeyObject));
}

TurkeyVariable turkey_object_get_property(TurkeyVM *vm, TurkeyObject *object, TurkeyString *name) {
	/* look up property */
	unsigned int index = fast_mod(name->hash, object->size);
	TurkeyObjectProperty *prop = object->properties[index];

	while(prop != 0) {
		if(prop->key == name)
			/* found it */
			return prop->value;

		prop = prop->next;
	}

	/* property is not found */
	TurkeyVariable var;
	var.type = TT_Null;
	return var;
}

void turkey_object_set_property(TurkeyVM *vm, TurkeyObject *object, TurkeyString *name, const TurkeyVariable &value) {
	/* look up property */
	unsigned int index = fast_mod(name->hash, object->size);
	TurkeyObjectProperty *prop = object->properties[index];

	while(prop != 0) {
		if(prop->key == name) {
			/* found it, overwrite existing value */
			prop->value = value;
			return;
		}

		prop = prop->next;
	}

	/* not found, insert it */

	/* see if we need to grow */
	if(object->count >= object->size) {
		turkey_object_grow(vm, object);
		index = fast_mod(name->hash, object->size);
	}

	prop = (TurkeyObjectProperty *)turkey_allocate_memory(vm->tag, sizeof(TurkeyObjectProperty));
	prop->next = object->properties[index];
	prop->key = name;
	prop->value = value;

	object->properties[index] = prop;
	object->count++;
}

void turkey_object_grow(TurkeyVM *vm, TurkeyObject *object) {
	unsigned int new_size = object->size * 2;
	TurkeyObjectProperty **new_properties = (TurkeyObjectProperty **)turkey_allocate_memory(vm->tag, (sizeof (TurkeyObjectProperty *)) * new_size);

	/* clear new hash table */
	for(unsigned int i = 0; i < new_size; i++)
		new_properties[i] = 0;

	/* add each existing property to the new hash table */
	for(unsigned int i = 0; i < object->size; i++) {
		TurkeyObjectProperty *prop = object->properties[i];
		while(prop != 0) {
			TurkeyObjectProperty *next = prop->next;

			unsigned int index = fast_mod(prop->key->hash, new_size);
			
			prop->next = new_properties[index];
			new_properties[index] = prop->next;

			prop = next;
		}
	}
	
	/* release the old hash table */
	turkey_free_memory(vm->tag, object->properties, (sizeof (TurkeyObjectProperty *)) * object->size);
	object->properties = object->properties;
	object->size = new_size;
}

void turkey_object_delete_property(TurkeyVM *vm, TurkeyObject *object, TurkeyString *name) {
	/* look up property */
	unsigned int index = fast_mod(name->hash, object->size);
	TurkeyObjectProperty *prop = object->properties[index];
	TurkeyObjectProperty *prev = 0;

	while(prop != 0) {
		if(prop->key == name) {
			/* found it, remove it */
			if(prev != 0)
				prev->next = prop->next;
			else
				object->properties[index] = prop->next;

			turkey_free_memory(vm->tag, prop, sizeof(TurkeyObjectProperty));
			return;
		}

		prev = prop;
		prop = prop->next;
	}

	/* not found, nothing to delete */
}

void turkey_object_call_operator(TurkeyVM *vm, TurkeyObject *object, TurkeyString *oper, TurkeyVariable &operand) {
	TurkeyVariable var = turkey_object_get_property(vm, object, oper);
	TurkeyVariable ret;

	if(var.type != TT_FunctionPointer)
		ret.type = TT_Null;
	else {
		vm->variable_stack.Push(operand);
		ret = turkey_call_function(vm, var.function, 1);
	}

	vm->variable_stack.Push(ret);
}

void turkey_object_call_operator(TurkeyVM *vm, TurkeyObject *object, TurkeyString *oper) {
	TurkeyVariable var = turkey_object_get_property(vm, object, oper);
	TurkeyVariable ret;

	if(var.type != TT_FunctionPointer)
		ret.type = TT_Null;
	else {
		ret = turkey_call_function(vm, var.function, 0);
	}

	vm->variable_stack.Push(ret);
}
