#include "io.h"

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
