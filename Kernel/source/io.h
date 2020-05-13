#pragma once
#include "types.h"

extern void memcpy(unsigned char *dest, const unsigned char *src, size_t count);
extern void memset(unsigned char *dest, unsigned char val, size_t count);
extern void CopyString(const unsigned char* source, size_t buffer_size, size_t strlen, unsigned char* dest);
extern bool strcmp(void *a, void *b, size_t count);
extern size_t strlen(const char *str);
extern size_t strlen_s(const char *str, size_t max);
extern uint8 inportb (unsigned short _port);
extern void outportb (unsigned short _port, unsigned char _data);
extern int8 inportsb (unsigned short _port);
extern uint16 inportw (unsigned short _port);
extern void outportw (unsigned short _port, uint16 _data);
extern uint32 inportdw (unsigned short _port);
extern void outportdw (unsigned short _port, uint32 _data);

static inline void wrmsr(uint64 msr, uint64 value)
{
	uint32 low = value & 0xFFFFFFFF;
	uint32 high = value >> 32;
	asm volatile (
		"wrmsr"
		:
		: "c"(msr), "a"(low), "d"(high)
	);
}