#pragma once

#if defined(TEST)
#include <limits.h>

#include <climits>
#ifndef INT_MAX
#define INT_MAX __INT_MAX__
#endif
#include <sys/types.h>
#include <stddef.h>
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

#ifdef __cplusplus
#include "../../../Libraries/perception/public/perception/sentinels.h"

// Value indicating an error.
constexpr size_t kError = static_cast<size_t>(::perception::kErrorSentinel);

// Value indicating an out-of-memory condition.
constexpr size_t kOutOfMemory =
    static_cast<size_t>(::perception::kOutOfMemorySentinel);
#else
#define ERROR 0xFFFFFFFFFFFFFFFFULL
#define OUT_OF_MEMORY 0xFFFFFFFFFFFFFFFEULL
#endif

