#pragma once

#include "types.h"

// Reads a byte from a port.
extern uint8 ReadIOByte(unsigned short _port);

// Writes a byte to a port.
extern void WriteIOByte(unsigned short _port, unsigned char _data);

// Sets a model-specific register.
extern void WriteModelSpecificRegister(uint64 msr, uint64 value);