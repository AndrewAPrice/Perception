#pragma once

#ifdef __TEST__
#include <sys/types.h>
#else
typedef unsigned long long int size_t;
#endif
typedef unsigned long long int uint64;
typedef unsigned int uint32;
typedef unsigned short uint16;
typedef unsigned char uint8;

typedef signed long long int int64;
typedef signed int int32;
typedef signed short int16;
typedef signed char int8;

// Magic value for when there is an error.
#define ERROR 1

// Magic value for when we are out of memory.
#define OUT_OF_MEMORY 1
