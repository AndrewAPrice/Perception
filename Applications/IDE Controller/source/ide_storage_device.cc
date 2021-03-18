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

#include "ide_storage_device.h"

#include "ata.h"
#include "ide_types.h"
#include "perception/port_io.h"
#include "perception/shared_memory.h"
#include "perception/threads.h"

using ::perception::SharedMemory;
using ::perception::Read8BitsFromPort;
using ::perception::Read16BitsFromPort;
using ::perception::Write8BitsToPort;
using ::perception::Write16BitsToPort;
using ::perception::Yield;
using ::permebuf::perception::devices::StorageDevice;
using ::permebuf::perception::devices::StorageType;

IdeStorageDevice::IdeStorageDevice(IdeDevice* device) : device_(device) {}

void IdeStorageDevice::HandleGetDeviceDetails(::perception::ProcessId sender,
	const StorageDevice::GetDeviceDetailsRequest& request,
	PermebufMiniMessageReplier<StorageDevice::GetDeviceDetailsResponse> responder) {
	StorageDevice::GetDeviceDetailsResponse response;
	response.SetSizeInBytes(device_->size_in_bytes);
	response.SetIsWritable(device_->is_writable);
	response.SetType(StorageType::Optical);
	responder.Reply(response);
}

void IdeStorageDevice::HandleRead(::perception::ProcessId sender,
	const StorageDevice::ReadRequest& request,
	PermebufMiniMessageReplier<StorageDevice::ReadResponse> responder) {
	SharedMemory destination_shared_memory = request.GetBuffer();
	if (!destination_shared_memory.Join()) {
		responder.Reply(StorageDevice::ReadResponse());
		return;
	}

	size_t bytes_to_copy = request.GetBytesToCopy();
	size_t device_offset_start = request.GetOffsetOnDevice();
	size_t buffer_offset_start = request.GetOffsetInBuffer();
	uint8* destination_buffer = (uint8*)*destination_shared_memory;

	if (bytes_to_copy == 0 ||
		device_offset_start + bytes_to_copy > device_->size_in_bytes ||
		buffer_offset_start + bytes_to_copy > destination_shared_memory.GetSize()) {
		// Nothing to copy or the region overflows.
		responder.Reply(StorageDevice::ReadResponse());
		return;
	}

	/* select drive - master/slave */
	uint16 bus = device_->channel ? ATA_BUS_SECONDARY : ATA_BUS_PRIMARY;
	Write8BitsToPort(ATA_DRIVE_SELECT(bus),device_->drive << 4);
	/* wait 400ns */
	ATA_SELECT_DELAY(bus);

	/* set features register to 0 (PIO Mode) */
	Write8BitsToPort(ATA_FEATURES(bus), 0x0);

	/* set lba1 and lba2 registers to 0x0008 (number of bytes will be returned ) */
	Write8BitsToPort(ATA_ADDRESS2(bus), ATAPI_SECTOR_SIZE & 0xFF);
	Write8BitsToPort(ATA_ADDRESS3(bus), ATAPI_SECTOR_SIZE >> 8);

	size_t start_lba = device_offset_start / ATAPI_SECTOR_SIZE;
	size_t end_lba = (device_offset_start + bytes_to_copy + ATAPI_SECTOR_SIZE - 1) /
		ATAPI_SECTOR_SIZE;
	for (size_t lba = start_lba; lba <= end_lba; lba++) {
		/* send packet command */
		Write8BitsToPort(ATA_COMMAND(bus), 0xA0);

		/* poll */
		uint8 status;
		while((status = Read8BitsFromPort(ATA_COMMAND(bus))) & 0x80) /* busy */
			Yield();


		while(!((status = Read8BitsFromPort(ATA_COMMAND(bus))) & 0x8) && !(status & 0x1))
			Yield();


		/* is there an error ? */
		if(status & 0x1) {
			/* no disk */
			responder.Reply(SD::ReadResponse());
			return;
		}

		/* send the atapi packet - must be 6 words (12 bytes) long */
		uint8 atapi_packet[12] = {0xA8, 0,
			uint8((lba >> 0x18) & 0xFF),
			uint8((lba >> 0x10) & 0xFF),
			uint8((lba >> 0x08) & 0xFF),
			uint8((lba >> 0x00) & 0xFF), 0, 0, 0, 1, 0, 0};

		for(status = 0; status < 12; status += 2) {
			Write16BitsToPort(ATA_DATA(bus),*(uint16 *)&atapi_packet[status]);
		}

		/* todo - wait for an irq */
		for(status = 0; status < 15; status++)
			Yield();

		/* read in the data */
		size_t indx = lba * ATAPI_SECTOR_SIZE;
		for(size_t i = 0; i < ATAPI_SECTOR_SIZE; i+=2, indx+=2) {
			uint16 b = Read16BitsFromPort(ATA_DATA(bus));

			if(indx == device_offset_start + bytes_to_copy - 1) {
				// Copy just the last byte.
				destination_buffer[indx - buffer_offset_start] = (b > 8) & 0xFF;
			} else if (indx + 1 == device_offset_start) {
				// Copy just the first byte.
				destination_buffer[indx - buffer_offset_start] = b & 0xFF;
			} else if(indx >= device_offset_start &&
				indx < device_offset_start + bytes_to_copy) {
				// Copy both bytes.
				*(uint16 *)&destination_buffer[indx - buffer_offset_start] = b;
			}
		}
	}

	responder.Reply(StorageDevice::ReadResponse());
}