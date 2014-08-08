#ifndef HOOKS_H
#define HOOKS_H

//#ifndef TEST
//typedef unsigned long long int size_t;
//#endif
#include <stdio.h>

struct TurkeyVM;
struct TurkeyString;

/* Implement these functions if you want to port Turkey */
/* Opens a file and returns a pointer, and the size of the pointer.
   It needs to be freed by calling turkey_free_memory(). */
extern void *turkey_load_file(void *tag, TurkeyString *path, size_t &size);

/* Allocate some memory and return a pointer to it. */
extern void *turkey_allocate_memory(void *tag, size_t size);

/* Allocate some executable memory and return a pointer to it. */
extern void *turkey_allocate_executable_memory(void *tag, size_t size);

/* Free some memory */
extern void turkey_free_memory(void *tag, void *mem, size_t size);

/* Realloctes memory */
extern void *turkey_reallocate_memory(void *tag, void *mem, size_t old_size, size_t new_size);

/* Copy memory from source to destination */
extern void turkey_memory_copy(void *dest, const void *src, size_t size);

/* Compare two blocks of memory */
extern bool turkey_memory_compare(const void *a, const void *b, size_t size);

/* Clear a block of memory */
extern void turkey_memory_clear(void *dest, size_t size);

/* Print string to buffer (sprintf_s), size is both buffer size, and outputs the string length */
extern void turkey_print_string(char *buffer, size_t &size, const char *format, ...);

extern TurkeyString *turkey_relative_to_absolute_path(TurkeyVM *vm, TurkeyString *relativePath);

extern double turkey_float_modulo(double a, double b);

#endif
