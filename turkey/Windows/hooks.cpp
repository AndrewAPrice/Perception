#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../hooks.h"

void *turkey_loadfile(const char *path, size_t &size) {
	/* open the file */
	FILE *f;
	fopen_s(&f, path, "r");
	fseek(f, 0, SEEK_END);
	size = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	 
	/* copy it into a buffer */
	void *ptr = malloc(size);
	fread(ptr, 1, size, f);

	/* close the file */
	fclose(f);

	return ptr;
}

void *turkey_allocate_memory(size_t size) {
	return malloc(size);
}

void turkey_free_memory(void *mem) {
	free(mem);
}

void *turkey_reallocate_memory(void *mem, size_t new_size) {
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

