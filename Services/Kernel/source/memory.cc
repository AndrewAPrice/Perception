#include "memory.h"

#include "physical_allocator.h"

#ifdef __TEST__
#include <stdio.h>
#else
extern "C" void memcpy(char *dest, const char *src, size_t count) {
  while (count > 0) {
    *dest = *src;
    dest++;
    src++;
    count--;
  }
}

extern "C" void memset(char *dest, char val, size_t count) {
  while (count > 0) {
    *dest = val;
    dest++;
    count--;
  }
}
#endif
