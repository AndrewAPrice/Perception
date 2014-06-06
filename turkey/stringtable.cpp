#include "turkey_internal.h"
#include "hooks.h"
#include "external/cityhash/city.h"

void turkey_stringtable_init(TurkeyVM *vm) {
	/* set up a hash table and initialize it blank */

	vm->string_table.count = 0;
	vm->string_table.length = 64;
	vm->string_table.strings = (TurkeyString **)turkey_allocate_memory((sizeof (TurkeyString*)) * vm->string_table.length);

	for(unsigned int i = 0; i < vm->string_table.count; i++)
		vm->string_table.strings[0] = 0;

	/* initialize the strings */
	
	/* some static strings, used in the VM */
	vm->string_table.s_true = turkey_stringtable_newstring(vm, "true", 4);
	vm->string_table.s_false = turkey_stringtable_newstring(vm, "false", 5);

	vm->string_table.s_boolean = turkey_stringtable_newstring(vm, "boolean", 7);
	vm->string_table.s_unsigned = turkey_stringtable_newstring(vm, "unsigned", 8);
	vm->string_table.s_signed = turkey_stringtable_newstring(vm, "signed", 6);
	vm->string_table.s_float = turkey_stringtable_newstring(vm, "float", 5);
	vm->string_table.s_null = turkey_stringtable_newstring(vm, "null", 4);
	vm->string_table.s_object = turkey_stringtable_newstring(vm, "object", 6);
	vm->string_table.s_array  = turkey_stringtable_newstring(vm, "array", 5);
	vm->string_table.s_buffer = turkey_stringtable_newstring(vm, "buffer", 6);
	vm->string_table.s_function = turkey_stringtable_newstring(vm, "function", 8);
	vm->string_table.s_string = turkey_stringtable_newstring(vm, "string", 6);

	/* string symbols */
	vm->string_table.ss_add = turkey_stringtable_newstring(vm, "+", 1);
	vm->string_table.ss_subtract = turkey_stringtable_newstring(vm, "-", 1);
	vm->string_table.ss_divide = turkey_stringtable_newstring(vm, "/", 1);
	vm->string_table.ss_multiply = turkey_stringtable_newstring(vm, "*", 1);
	vm->string_table.ss_modulo = turkey_stringtable_newstring(vm, "%", 1);
	vm->string_table.ss_increment = turkey_stringtable_newstring(vm, "++", 2);
	vm->string_table.ss_decrement = turkey_stringtable_newstring(vm, "--", 2);
	vm->string_table.ss_xor = turkey_stringtable_newstring(vm, "^", 1);
	vm->string_table.ss_and = turkey_stringtable_newstring(vm, "&", 1);
	vm->string_table.ss_or = turkey_stringtable_newstring(vm, "|", 1);
	vm->string_table.ss_not = turkey_stringtable_newstring(vm, "!", 1);
	vm->string_table.ss_shift_left = turkey_stringtable_newstring(vm, "<<", 2);
	vm->string_table.ss_shift_right = turkey_stringtable_newstring(vm, ">>", 2);
	vm->string_table.ss_rotate_left = turkey_stringtable_newstring(vm, "<<<", 3);
	vm->string_table.ss_rotate_right = turkey_stringtable_newstring(vm, ">>>", 3);
	vm->string_table.ss_less_than = turkey_stringtable_newstring(vm, "<", 1);
	vm->string_table.ss_greater_than = turkey_stringtable_newstring(vm, ">", 1);
	vm->string_table.ss_less_than_or_equals = turkey_stringtable_newstring(vm, "<=", 2);
	vm->string_table.ss_greater_than_or_equals = turkey_stringtable_newstring(vm, ">=", 2);
}

void turkey_stringtable_cleanup(TurkeyStringTable &table) {
	/* deallocate each string */
	for(unsigned int i = 0; i < table.length; i++) {
		TurkeyString *s = table.strings[i];
		/* each string at this hash entry */
		while(s != 0) {
			TurkeyString *str = s;
			s = s->next; /* point to the next string before we do anything, as ->next will can change as we unallocate it*/

			turkey_free_memory(str->string);
			turkey_free_memory(str);
		}
	}

	turkey_free_memory(table.strings);
}

TurkeyString *turkey_stringtable_newstring(TurkeyVM *vm, const char *string, unsigned int length) {
	TurkeyStringTable &table = vm->string_table;

	/* hash the string */
	unsigned int hash;
	if(length > 512) // 128-bit hash on long functions
		hash = (unsigned int)Hash128to64(CityHash128(string, length));
	else // 64-bit has on short functions
		hash = (unsigned int)CityHash64WithSeed(string, length, (uint64)length);

	/* look for this in the hash table */
	unsigned int index = fast_mod(hash, table.length);
	TurkeyString *s = table.strings[index];
	while(s != 0) {
		if(s->length == length && turkey_memory_compare(string, s->string, length))
			return s; /* found it! */

		s = s->next;
	}

	/* not found, add it to the hash table */
	if(table.count >= table.length) {
		turkey_stringtable_grow(table);
		index = fast_mod(hash, table.length);
	}

	table.count++;

	s = (TurkeyString *)turkey_allocate_memory(sizeof TurkeyString);
	s->string = (char *)turkey_allocate_memory(length);
	turkey_memory_copy(s->string, string, length);
	s->length = length;
	s->hash = hash;
	s->prev = 0;
	s->next = table.strings[index];

	if(table.strings[index] != 0)
		table.strings[index]->prev = s;

	table.strings[index] = s;

	/* register the string with the garbage collector */
	turkey_gc_register_string(vm->garbage_collector, s);

	return s;
}

void turkey_stringtable_grow(TurkeyStringTable &table) {
	/* double the size of the string table */
	unsigned int new_size = table.length * 2;
	TurkeyString **new_strings = (TurkeyString **)turkey_allocate_memory((sizeof (TurkeyString*)) * new_size);

	/* initialize it blank */
	for(unsigned int i = 0; i < new_size; i++)
		new_strings[i] = 0;

	/* copy each string into the new hash table */
	for(unsigned int i = 0; i < table.length; i++) {
		TurkeyString *s = table.strings[i];
		/* each string at this hash entry */
		while(s != 0) {
			TurkeyString *str = s;
			s = s->next; /* point to the next string before we do anything, as ->next will can change as we rehash */

			str->prev = 0;
			unsigned int index = fast_mod(str->hash, new_size);

			/* add it to the new hash table */
			str->next = new_strings[index];

			if(new_strings[index] != 0)
				new_strings[index]->prev = str;

			new_strings[index] = str;
		}
	}

	turkey_free_memory(table.strings);
	table.strings = new_strings;
	table.length = new_size;
}

/* removes a string - invoked by the garbage collector */
void turkey_stringtable_removestring(TurkeyStringTable &table, TurkeyString *string) {
	/* remove us from the hash table */
	if(string->prev != 0)
		string->prev->next = string->next;
	else {
		unsigned int index = fast_mod(string->hash, table.length);
		table.strings[index] = string->next;
	}

	if(string->next != 0)
		string->next->prev = string->prev;

	table.count--;

	/* free the memory */
	turkey_free_memory(string->string);
	turkey_free_memory(string);
}
