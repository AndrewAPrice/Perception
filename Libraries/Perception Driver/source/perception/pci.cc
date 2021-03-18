// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "perception/pci.h"

#include "perception/port_io.h"

using ::perception::Read32BitsFromPort;
using ::perception::Write32BitsToPort;

namespace perception {

uint8 Read8BitsFromPciConfig(uint8 bus, uint8 slot, uint8 func, uint8 offset) {
	uint16 word = Read16BitsFromPciConfig(bus, slot, func, offset & 0xFE);
	if(offset & 1)
		return (uint8)(word >> 8);
	else
		return (uint8)(word & 0xFF);
}

uint16 Read16BitsFromPciConfig(uint8 bus, uint8 slot, uint8 func, uint8 offset) {
	uint32 lbus = (uint32)bus;
	uint32 lslot = (uint32)slot;
	uint32 lfunc = (uint32)func;
	
	/* bits:
		31 - enabled bit
		30-24 - reserved
		23-16 - bus number
		15-11 - device number
		10-8 - function number
		7-2 - register number
		1-0 - 00 */

	uint32 address = (uint32)((lbus << 16) | (lslot << 11) |
		(lfunc << 8) | (offset & 0xFC) | ((uint32)0x80000000));

	/* write out the address */
	Write32BitsToPort(0xCF8, address);

	/* read in the data */
	return (uint16)((Read32BitsFromPort(0xCFC) >> (((uint16)offset & 2) * 8)) & 0xFFFF);
}

uint32 Read32BitsFromPciConfig(uint8 bus, uint8 slot, uint8 func, uint8 offset) {
	uint32 lbus = (uint32)bus;
	uint32 lslot = (uint32)slot;
	uint32 lfunc = (uint32)func;

	uint32 address = (uint32)((lbus << 16) | (lslot << 11) |
		(lfunc << 8) | offset | ((uint32)0x80000000));

	/* write out the address */
	Write32BitsToPort(0xCF8, address);

	/* read in the data */
	return Read32BitsFromPort(0xCFC);
}

}
