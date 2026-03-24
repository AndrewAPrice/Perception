#include <errno.h>

extern "C" long __syscall_ret(long r)
{
	if (r > -4096UL) {
		errno = -r;
		return -1;
	}
	return r;
}
