#pragma once
#include "types.h"
#ifdef __TEST__
#include <string.h>

#else

#include "text_terminal.h"

// Copies `count` bytes from `src` to `dest`.
extern "C" void memcpy(char *dest, const char *src, size_t count);

// Sets `count` bytes in `dest` to `val`.
extern "C" void memset(char *dest, char val, size_t count);
#endif

// Clears an object to 0.
template <class T>
void Clear(T &object) {
  memset((char *)&object, 0, sizeof(T));
}

// Copies a string.
extern void CopyString(const char *source, size_t buffer_size, size_t strlen,
                       char *dest);

#ifndef __TEST__
// Compares two strings and returns if they're equal.
extern bool strcmp(void *a, void *b, size_t count);

// Measures the size of a string.
extern size_t strlen(const char *str);
#endif

// Measures the size of a string with a maximum length.
extern size_t strlen_s(const char *str, size_t max);

// Reads a byte from a port.
extern uint8 ReadIOByte(unsigned short _port);

// Writes a byte to a port.
extern void WriteIOByte(unsigned short _port, unsigned char _data);

// Sets a model-specific register.
extern void WriteModelSpecificRegister(uint64 msr, uint64 value);