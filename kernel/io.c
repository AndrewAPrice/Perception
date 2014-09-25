#include "io.h"

void memcpy(unsigned char *dest, const unsigned char *src, size_t count) {
	while(count > 0) {
		*dest = *src;
		dest++;
		src++;
		count--;
	}
}

void memset(unsigned char *dest, unsigned char val, size_t count) {
	while(count > 0) {
		*dest = val;
		dest++;
		count--;
	}
}

bool strcmp(void *a, void *b, size_t count) {
	unsigned char *ac = (unsigned char*)a;
	unsigned char *bc = (unsigned char*)b;

	while(count > 0) {
		if(*ac != *bc)
			return true;
		ac++;
		bc++;
		count--;
	}

	return false;
}

size_t strlen(const char *str) {
	size_t count = 0;
	while(*str) {
		count++;
		str++;
	}

	return count;
}

unsigned char inportb (unsigned short _port)
{
    unsigned char rv;
    __asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}

void outportb (unsigned short _port, unsigned char _data)
{
    __asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}
