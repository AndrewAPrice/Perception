#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Reads a byte from a port.
uint8 ReadIOByte(unsigned short _port);

// Writes a byte to a port.
void WriteIOByte(unsigned short _port, unsigned char _data);

#ifdef __cplusplus
}
#endif

// Sets a model-specific register.
void WriteModelSpecificRegister(uint64 msr, uint64 value);

// Reads a model-specific register.
uint64 ReadModelSpecificRegister(uint64 msr);

// Reads the CPU Time Stamp Counter (TSC).
uint64 ReadTimestampCounter();

// Executes the CPUID instruction.
void GetCpuId(uint32 leaf, uint32* eax, uint32* ebx, uint32* ecx, uint32* edx);

