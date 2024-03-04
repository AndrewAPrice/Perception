#include "io.h"

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

void CopyString(const char *source, size_t buffer_size, size_t strlen,
                char *dest) {
  // Leave room for a null terminator.
  if (strlen > buffer_size - 1) {
    strlen = buffer_size - 1;
  }

  memcpy(dest, source, strlen);

  dest += strlen;
  memset(dest, '\0', buffer_size - strlen);
}

#ifndef __TEST__
bool strcmp(void *a, void *b, size_t count) {
  unsigned char *ac = (unsigned char *)a;
  unsigned char *bc = (unsigned char *)b;

  while (count > 0) {
    if (*ac != *bc) return true;
    ac++;
    bc++;
    count--;
  }

  return false;
}

size_t strlen(const char *str) {
  size_t count = 0;
  while (*str) {
    count++;
    str++;
  }

  return count;
}
#endif

size_t strlen_s(const char *str, size_t max_size) {
  size_t count = 0;
  while (*str && count < max_size) {
    count++;
    str++;
  }

  return count;
}

uint8 ReadIOByte(unsigned short _port) {
#ifdef __TEST__
  printf("ReadIOByte called in test.\n");
  return 0;
#else
  unsigned char rv;
  __asm__ __volatile__("inb %1, %0" : "=a"(rv) : "dN"(_port));
  return rv;
#endif
}

void WriteIOByte(unsigned short _port, unsigned char _data) {
#ifdef __TEST__
  printf("WriteIOByte called in test.\n");
#else
  __asm__ __volatile__("outb %1, %0" : : "dN"(_port), "a"(_data));
#endif
}


void WriteModelSpecificRegister(uint64 msr, uint64 value) {
#ifdef __TEST__
  printf("WriteModelSpecificRegister called in test.\n");
#else
  uint32 low = value & 0xFFFFFFFF;
  uint32 high = value >> 32;
  asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
#endif
}
