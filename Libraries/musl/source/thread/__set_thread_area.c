#include "pthread_impl.h"

extern void DebugPrint(char *str);

int __set_thread_area(void *p)
{
#ifdef SYS_set_thread_area
	return __syscall(SYS_set_thread_area, p);
#else
	return -ENOSYS;
#endif
}
