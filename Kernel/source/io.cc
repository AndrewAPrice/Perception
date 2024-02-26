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

uint8 inportb(unsigned short _port) {
#ifdef __TEST__
  printf("inportb called in test.\n");
  return 0;
#else
  unsigned char rv;
  __asm__ __volatile__("inb %1, %0" : "=a"(rv) : "dN"(_port));
  return rv;
#endif
}

void outportb(unsigned short _port, unsigned char _data) {
#ifdef __TEST__
  printf("inportb called in test.\n");
#else
  __asm__ __volatile__("outb %1, %0" : : "dN"(_port), "a"(_data));
#endif
}

int8 inportsb(unsigned short _port) {
#ifdef __TEST__
  printf("inportsb called in test.\n");
  return 0;
#else
  int8 rv;
  __asm__ __volatile__("inb %1, %0" : "=a"(rv) : "dN"(_port));
  return rv;
#endif
}

uint16 inportw(unsigned short _port) {
#ifdef __TEST__
  printf("inportw called in test.\n");
  return 0;
#else
  uint16 rv;
  __asm__ __volatile__("inw %1, %0" : "=a"(rv) : "dN"(_port));
  return rv;
#endif
}

void outportw(unsigned short _port, uint16 _data) {
#ifdef __TEST__
  printf("outportw called in test.\n");
#else
  __asm__ __volatile__("outw %1, %0" : : "dN"(_port), "a"(_data));
#endif
}

uint32 inportdw(unsigned short _port) {
#ifdef __TEST__
  printf("inportdw called in test.\n");
  return 0;
#else
  uint32 rv;
  __asm__ __volatile__("inl %1, %0" : "=a"(rv) : "dN"(_port));
  return rv;
#endif
}

void outportdw(unsigned short _port, uint32 _data) {
#ifdef __TEST__
  printf("outportdw called in test.\n");
#else
  __asm__ __volatile__("outl %1, %0" : : "dN"(_port), "a"(_data));
#endif
}

void wrmsr(uint64 msr, uint64 value) {
#ifdef __TEST__
  printf("wrmsr called in test.\n");
#else
  uint32 low = value & 0xFFFFFFFF;
  uint32 high = value >> 32;
  asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
#endif
}
