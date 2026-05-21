#ifndef TEST
#include "io.h"

uint8 ReadIOByte(unsigned short _port) {
  unsigned char rv;
  __asm__ __volatile__("inb %1, %0" : "=a"(rv) : "dN"(_port));
  return rv;
}

void WriteIOByte(unsigned short _port, unsigned char _data) {
  __asm__ __volatile__("outb %1, %0" : : "dN"(_port), "a"(_data));
}

void WriteModelSpecificRegister(uint64 msr, uint64 value) {
  uint32 low = value & 0xFFFFFFFF;
  uint32 high = value >> 32;
  asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

#endif // TEST
