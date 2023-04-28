#pragma once
#include "types.h"
#ifdef __TEST__
#include <string.h>

#else

// Copies `count` bytes from `src` to `dest`.
extern void memcpy(unsigned char *dest, const unsigned char *src, size_t count);

// Sets `count` bytes in `dest` to `val`.
extern void memset(unsigned char *dest, unsigned char val, size_t count);
#endif

// Copies a string.
extern void CopyString(const unsigned char *source, size_t buffer_size,
                       size_t strlen, unsigned char *dest);

#ifndef __TEST__
// Compares two strings and returns if they're equal.
extern bool strcmp(void *a, void *b, size_t count);

// Measures the size of a string.
extern size_t strlen(const char *str);
#endif

// Measures the size of a string with a maximum length.
extern size_t strlen_s(const char *str, size_t max);

// Reads a byte from a port.
extern uint8 inportb(unsigned short _port);

// Writes a byte to a port.
extern void outportb(unsigned short _port, unsigned char _data);

// Reads a signed byte from a port.
extern int8 inportsb(unsigned short _port);

// Reads a 16-bit unsigned integer from a port.
extern uint16 inportw(unsigned short _port);

// Writes a 16-bit unsigned integer to a port.
extern void outportw(unsigned short _port, uint16 _data);

// Reads a 32-bit unsigned integer from a port.
extern uint32 inportdw(unsigned short _port);

// Writes a 32-bit unsigned integer to port.
extern void outportdw(unsigned short _port, uint32 _data);

// Sets a model-specific register.
extern void wrmsr(uint64 msr, uint64 value);