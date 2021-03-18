// Copyright 2021 Google LLC
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

#include "io.h"

#include "ata.h"
#include "ide_types.h"
#include "perception/port_io.h"

using ::perception::Read8BitsFromPort;
using ::perception::Read32BitsFromPort;
using ::perception::Write8BitsToPort;

void WriteByteToIdeController(IdeChannelRegisters *channel, uint8 reg, uint8 data) {
	if(reg > 0x07 && reg < 0x0C)
		WriteByteToIdeController(channel, ATA_REG_CONTROL, 0x80 | channel->no_interrupt);
	if(reg < 0x08)
		Write8BitsToPort(channel->io_base + reg - 0x00, data);
	else if (reg < 0x0C)
		Write8BitsToPort(channel->io_base + reg - 0x06, data);
	else if(reg < 0x0E)
		Write8BitsToPort(channel->control_base + reg - 0x0A, data);
	else if(reg < 0x16)
		Write8BitsToPort(channel->bus_master_id + reg - 0x0E, data);
	if(reg > 0x07 && reg < 0x0C)
		WriteByteToIdeController(channel, ATA_REG_CONTROL, channel->no_interrupt);
}

uint8 ReadByteFromIdeController(IdeChannelRegisters *channel, uint8 reg) {
	uint8 out;

	if(reg > 0x07 && reg < 0x0C)
		WriteByteToIdeController(channel, ATA_REG_CONTROL, 0x80 | channel->no_interrupt);
	if(reg < 0x08)
		out = Read8BitsFromPort(channel->io_base + reg - 0x00);
	else if (reg < 0x0C)
		out = Read8BitsFromPort(channel->io_base + reg - 0x06);
	else if(reg < 0x0E)
		out = Read8BitsFromPort(channel->control_base + reg - 0x0A);
	else if(reg < 0x16)
		out = Read8BitsFromPort(channel->bus_master_id + reg - 0x0E);
	else
		out = 0;

	if(reg > 0x07 && reg < 0x0C)
		WriteByteToIdeController(channel, ATA_REG_CONTROL, channel->no_interrupt);

	return out;
}

void ReadBytesFromIdeControllerIntoBuffer(IdeChannelRegisters *channel, uint8 reg, void *buffer, size_t quads) {
	if(reg > 0x07 && reg < 0x0C)
		WriteByteToIdeController(channel, ATA_REG_CONTROL, 0x80 | channel->no_interrupt);

	uint16 port;

	if(reg < 0x08)
		port = channel->io_base + reg - 0x00;
	else if(reg < 0x0C)
		port = channel->io_base + reg - 0x06;
	else if(reg < 0x0E)
		port = channel->control_base + reg - 0x0A;
	else if(reg < 0x16)
		port = channel->bus_master_id + reg - 0x0E;
	else
		port = 0;

	if(port != 0) {
		uint32 *ptr = static_cast<uint32*>(buffer);
		for(int i = 0; i < quads; i++, ptr++)
			*ptr = Read32BitsFromPort(port);
	}

	if(reg > 0x07 && reg < 0x0C)
		WriteByteToIdeController(channel, ATA_REG_CONTROL, channel->no_interrupt);
}
