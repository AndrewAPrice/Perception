#pragma once

#include "types.h"

// Reads a byte from a port.
uint8 ReadIOByte(unsigned short _port);

// Writes a byte to a port.
void WriteIOByte(unsigned short _port, unsigned char _data);

// Sets a model-specific register.
void WriteModelSpecificRegister(uint64 msr, uint64 value);
