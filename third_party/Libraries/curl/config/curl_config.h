#pragma once
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdint.h>
#include <limits.h>

#define BUILDING_LIBCURL 1
#define HAVE_CONFIG_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRUCT_TIMEVAL 1
#define HAVE_SOCKET 1
#define HAVE_SELECT 1
#include <poll.h>
#define HAVE_POLL 1
#define HAVE_POLL_FINE 1
#define HAVE_RECV 1
#define HAVE_SEND 1
#define HAVE_LONGLONG 1
#define CURL_OS "Perception"
#define OS "Perception"
#define SIZEOF_CURL_OFF_T 8
#define SIZEOF_LONG 8
#define SIZEOF_SIZE_T 8
#define SIZEOF_TIME_T 8
#define CURL_TYPEOF_CURL_OFF_T long long
#define CURL_FORMAT_CURL_OFF_T "lld"
#define CURL_FORMAT_CURL_OFF_TU "llu"
#define CURL_SUFFIX_CURL_OFF_T LL
#define CURL_SUFFIX_CURL_OFF_TU ULL
#define CURL_OFF_T_MAX 0x7FFFFFFFFFFFFFFFLL
#define CURL_OFF_T_MIN (-CURL_OFF_T_MAX - 1)
#define sread(x,y,z) read(x,y,z)
#define swrite(x,y,z) write(x,y,z)
#define sclose(x) close(x)
