#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef DEBUG
#include <assert.h>
#endif

struct ProcessLaunchInfo;

/* a kernel panic - we shouldn't return from this! */
extern void platform_kernel_panic(char *message);

extern void platform_graphics_initialize(); /* initialize graphics */
extern void platform_mouse_initialize(); /* initialize mouse */
extern void platform_keyboard_initialize(); /* intiialize keyboard */

extern bool platform_thread_create(ProcessLaunchInfo *tag); /* create a thread, with this entry point */

extern void *platform_allocate_memory(size_t size);
extern void platform_free_memory(void *ptr);
extern void platform_memory_copy(void *dest, void *src, size_t size);

extern void start_scheduling();

#endif
