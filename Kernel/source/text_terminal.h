#pragma once
#include "types.h"

// Prints a single character.
extern void PrintChar(char c);

// Prints a null-terminated string.
extern void PrintString(const char *str);

// Prints a fixed length string.
extern void PrintFixedString(const char *str, size_t length);

// Prints a number as a 64-bit hexidecimal string.
extern void PrintHex(size_t h);

// Prints a number as a decimal string.
extern void PrintNumber(size_t n);

// Prints a number as a decimal string without commas.
extern void PrintNumberWithoutCommas(size_t n);