#ifndef TEST
#include "hardware/io.h"

namespace hardware {

uint8 ReadIOByte(unsigned short port) {
  unsigned char rv;
  __asm__ __volatile__("inb %1, %0" : "=a"(rv) : "dN"(port));
  return rv;
}

void WriteIOByte(unsigned short port, unsigned char data) {
  __asm__ __volatile__("outb %1, %0" : : "dN"(port), "a"(data));
}

uint16 ReadIO16Bits(unsigned short port) {
  uint16 rv;
  __asm__ __volatile__("inw %1, %0" : "=a"(rv) : "dN"(port));
  return rv;
}

void WriteIO16Bits(unsigned short port, unsigned short data) {
  __asm__ __volatile__("outw %1, %0" : : "dN"(port), "a"(data));
}

void WriteModelSpecificRegister(uint64 msr, uint64 value) {
  uint32 low = value & 0xFFFFFFFF;
  uint32 high = value >> 32;
  asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

uint64 ReadModelSpecificRegister(uint64 msr) {
  uint32 low, high;
  asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
  return ((uint64)high << 32) | low;
}

uint64 ReadTimestampCounter() {
  uint32 low, high;
  asm volatile("rdtsc" : "=a"(low), "=d"(high));
  return ((uint64)high << 32) | low;
}

void GetCpuId(uint32 leaf, uint32& eax, uint32& ebx, uint32& ecx, uint32& edx) {
  asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf));
}

void GetCpuId(uint32 leaf, uint32 subleaf, uint32& eax, uint32& ebx, uint32& ecx,
              uint32& edx) {
  asm volatile("cpuid"
               : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
               : "a"(leaf), "c"(subleaf));
}

}  // namespace hardware

#endif // TEST
