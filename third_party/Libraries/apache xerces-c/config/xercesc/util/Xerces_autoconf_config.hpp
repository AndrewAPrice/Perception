#pragma once

#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <sys/types.h>
#include <wchar.h>
#include <unistd.h>
#include <sys/time.h>

#define XERCES_AUTOCONF 1
#define XERCES_HAVE_SYS_TYPES_H 1
#define XERCES_HAVE_INTTYPES_H 1
#define XERCES_HAVE_STDINT_H 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_SYS_TIME_H 1
#define HAVE_TOWUPPER 1
#define HAVE_TOWLOWER 1
#define HAVE_WCTYPE_H 1

#define XERCES_XMLCH_T uint16_t
#define XERCES_SIZE_T size_t
#define XERCES_SSIZE_T ssize_t

#define XERCES_HAS_CPP_NAMESPACE 1
#define XERCES_STD_NAMESPACE 1
#define XERCES_NEW_IOSTREAMS 1
#define XERCES_PLATFORM_EXPORT
#define XERCES_PLATFORM_IMPORT
#define XERCES_TEMPLATE_EXTERN

#define XERCES_USE_FILEMGR_POSIX 1
#define XERCES_USE_MUTEXMGR_POSIX 1
#define XERCES_USE_MSGLOADER_INMEMORY 1
#define XERCES_USE_TRANSCODER_ICONV 1
