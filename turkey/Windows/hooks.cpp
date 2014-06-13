#ifdef TEST
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "../hooks.h"
#include "../turkey.h"

size_t alloc_size = 0;
size_t amount_since_last_alloc = 0;
const size_t gc_alloc_threshold = 4 * 1024 * 1024; /* call the GC every time we allocate 4 mb */

void *turkey_load_file(void *tag, TurkeyString *path, size_t &size) {
	/* create a temporary null terminated string */
	char *str = (char *)malloc(path->length + 1);
	memcpy_s(str, path->length + 1, path->string, path->length);
	str[path->length] = '\0';

	/* open the file */
	FILE *f;
	if(fopen_s(&f, str, "r") != 0) {
		free(str);
		return 0;
	}
	free(str);

	fseek(f, 0, SEEK_END);
	size = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	 
	/* copy it into a buffer */
	void *ptr = malloc(size);
	fread(ptr, 1, size, f);

	/* close the file */
	fclose(f);

	alloc_size += size;

	return ptr;
}

void *turkey_allocate_memory(void *tag, size_t size) {
	alloc_size += size;
	amount_since_last_alloc += size;
	if(amount_since_last_alloc >= gc_alloc_threshold) {
		amount_since_last_alloc = 0;
		turkey_gc_collect((TurkeyVM *)tag);
	}
	//printf("Allocated %i now %i\n", size, alloc_size);
	return malloc(size);
}

void turkey_free_memory(void *tag, void *mem, size_t size) {
	free(mem);
	alloc_size -= size;
	//printf("Freed %i now %i\n", size, alloc_size);
}

void *turkey_reallocate_memory(void *tag, void *mem, size_t old_size, size_t new_size) {
	alloc_size -= old_size;
	alloc_size += new_size;

	
	if(new_size > old_size) {
		amount_since_last_alloc += new_size - old_size;
		if(amount_since_last_alloc >= gc_alloc_threshold) {
			amount_since_last_alloc = 0;
			turkey_gc_collect((TurkeyVM *)tag);
		}
	}

	return realloc(mem, new_size);
}

void turkey_memory_copy(void *dest, const void *src, size_t size) {
	memcpy(dest, src, size);
}

bool turkey_memory_compare(const void *a, const void *b, size_t size) {
	return memcmp(a, b, size) == 0;
}

void turkey_memory_clear(void *dest, size_t size) {
	memset(dest, 0, size);
}

void turkey_print_string(char *buffer, size_t &size, const char *format, ...) {
	va_list args;
	va_start(args, format);
	vsprintf_s(buffer, size, format, args);
	va_end(args);

	size = strlen(buffer);
}

extern TurkeyString *turkey_relative_to_absolute_path(TurkeyVM *vm, TurkeyString *relativePath) {
	char path[512];

	char *str_temp = (char *)malloc(relativePath->length + 1);
	memcpy(str_temp, relativePath->string, relativePath->length);
	str_temp[relativePath->length] = '\0';

	_fullpath(path, str_temp, 512);
	free(str_temp);

	size_t len = strnlen_s(path, 512);
	return turkey_stringtable_newstring(vm, path, len);
}

double turkey_float_modulo(double a, double b) {
	return fmod(a, b);
}
#endif