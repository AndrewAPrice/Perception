#include "hooks.h"
#include "turkey.h"

TurkeyString *turkey_string_escape(TurkeyVM *vm, TurkeyString *str) {
	turkey_gc_hold(vm, str, TT_String);
	
	// figure out the length of the string
	size_t strlength = 2; // quotes
	for(size_t i = 0; i < str->length; i++) {
		switch(str->string[i]) {
		case '\0': strlength += 2; break;
		case '\'': strlength += 2; break;
		case '"': strlength += 2; break;
		case '\\': strlength += 2; break;
		case '\n': strlength += 2; break;
		case '\t': strlength += 2; break;
		case '\r': strlength += 2; break;
		default: strlength += 1; break;
		}
	}

	// copy the string into it
	char *strbuffer = (char *)turkey_allocate_memory(vm->tag, strlength);
	char *ptr = strbuffer;
	*ptr = '"';
	ptr++;
	for(size_t i = 0; i < str->length; i++) {
		switch(str->string[i]) {
		case '\0':  *ptr = '\\'; ptr++; *ptr = '0'; ptr++; break;
		case '\'':  *ptr = '\\'; ptr++; *ptr = '\''; ptr++; break;
		case '"':  *ptr = '\\'; ptr++; *ptr = '"'; ptr++; break;
		case '\\':  *ptr = '\\'; ptr++; *ptr = '\\'; ptr++; break;
		case '\n':  *ptr = '\\'; ptr++; *ptr = 't'; ptr++; break;
		case '\t': *ptr = '\\'; ptr++; *ptr = 't'; ptr++; break;
		case '\r': *ptr = '\\'; ptr++; *ptr = 'r'; ptr++; break;
		default: *ptr = str->string[i]; ptr++; break;
		}
	}

	*ptr = '"';

	turkey_gc_unhold(vm, str, TT_String);

	TurkeyString *str_out = turkey_stringtable_newstring(vm, strbuffer, strlength);

	turkey_free_memory(vm->tag, strbuffer, strlength);

	return str_out;
}

TurkeyString *turkey_string_append(TurkeyVM *vm, TurkeyString *stra, TurkeyString *strb) {
	turkey_gc_hold(vm, stra, TT_String);
	turkey_gc_hold(vm, strb, TT_String);

	char *buffer = (char *)turkey_allocate_memory(vm->tag, stra->length + strb->length);

	turkey_memory_copy(buffer, stra->string, stra->length);
	turkey_memory_copy((char *)((size_t)buffer + stra->length), strb->string, strb->length);

	TurkeyString *str = turkey_stringtable_newstring(vm, buffer, stra->length + strb->length);

	turkey_free_memory(vm->tag, buffer, stra->length + strb->length);
	turkey_gc_unhold(vm, stra, TT_String);
	turkey_gc_unhold(vm, strb, TT_String);

	return str;
}

TurkeyString *turkey_string_substring(TurkeyVM *vm, TurkeyString *str, unsigned int start, unsigned int length) {
	if(start > str->length)
		start = str->length;

	if(start + length > str->length)
		length = str->length - start;

	if(length == 0)
		return vm->string_table.ss_blank;

	turkey_gc_hold(vm, str, TT_String);

	TurkeyString *substr = turkey_stringtable_newstring(vm, (char *)((size_t)str->string + start), length);

	turkey_gc_unhold(vm, str, TT_String);

	return substr;
}