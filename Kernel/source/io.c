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

void CopyString(const unsigned char* source, size_t buffer_size, size_t strlen, unsigned char* dest) {
	// Leave room for a null terminator.
	if (strlen > buffer_size - 1) {
		strlen = buffer_size - 1;
	}

	memcpy(dest, source, strlen);

	dest+= strlen;
	memset(dest, '\0', buffer_size - strlen);
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

uint8 inportb (unsigned short _port)
{
    unsigned char rv;
    __asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}

void outportb (unsigned short _port, unsigned char _data)
{
    __asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

int8 inportsb (unsigned short _port)
{
    int8 rv;
    __asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}

uint16 inportw (unsigned short _port) {
    uint16 rv;
    __asm__ __volatile__ ("inw %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}

void outportw (unsigned short _port, uint16 _data) {
    __asm__ __volatile__ ("outw %1, %0" : : "dN" (_port), "a" (_data));
}

uint32 inportdw (unsigned short _port) {
    uint32 rv;
    __asm__ __volatile__ ("inl %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}

void outportdw (unsigned short _port, uint32 _data) {
    __asm__ __volatile__ ("outl %1, %0" : : "dN" (_port), "a" (_data));
}