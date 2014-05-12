#ifndef HOOKS_H
#define HOOKS_H
/* Implement these functions if you want to port Turkey */
/* Opens a file and returns a pointer, and the size of the pointer.
   It needs to be freed by calling turkey_free_memory(). */
extern void *turkey_loadfile(const char *path, size_t &size);

/* Allocate some memory and return a pointer to it. */
extern void *turkey_allocate_memory(size_t size);

/* Free some memory */
extern void turkey_free_memory(void *mem);

/* Realloctes memory */
extern void *turkey_reallocate_memory(void *mem, size_t new_size);

/* Copy memory from source to destination */
extern void turkey_memory_copy(void *dest, const void *src, size_t size);

/* Compare two blocks of memory */
extern bool turkey_memory_compare(const void *a, const void *b, size_t size);

/* Clear a block of memory */
extern void turkey_memory_clear(void *dest, size_t size);

#endif
