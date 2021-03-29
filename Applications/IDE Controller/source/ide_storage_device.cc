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
#include "ide.h"
#include "ide_types.h"
#include "interrupts.h"
#include "perception/port_io.h"
#include "perception/shared_memory.h"
#include "perception/threads.h"
#include "perception/time.h"

using ::perception::SharedMemory;
using ::perception::Read8BitsFromPort;
using ::perception::Read16BitsFromPort;
using ::perception::Write8BitsToPort;
using ::perception::Write16BitsToPort;
using ::permebuf::perception::devices::StorageDevice;
using ::permebuf::perception::devices::StorageType;
using ::perception::SleepForDuration;

IdeStorageDevice::IdeStorageDevice(IdeDevice* device) : device_(device) {}

StatusOr<Permebuf<StorageDevice::GetDeviceDetailsResponse>>
	IdeStorageDevice::HandleGetDeviceDetails(::perception::ProcessId sender,
	const StorageDevice::GetDeviceDetailsRequest& request) {
	Permebuf<StorageDevice::GetDeviceDetailsResponse> response;
	response->SetSizeInBytes(device_->size_in_bytes);
	response->SetIsWritable(device_->is_writable);
	response->SetType(StorageType::Optical);
	response->SetName(device_->name);
	return response;
}

StatusOr<StorageDevice::ReadResponse>
	IdeStorageDevice::HandleRead(::perception::ProcessId sender,
	const StorageDevice::ReadRequest& request) {
	SharedMemory destination_shared_memory = request.GetBuffer();
	if (!destination_shared_memory.Join()) {
		return ::perception::Status::INVALID_ARGUMENT;
	}

	int64 bytes_to_copy = request.GetBytesToCopy();
	int64 device_offset_start = request.GetOffsetOnDevice();
	int64 buffer_offset_start = request.GetOffsetInBuffer();

	uint8* destination_buffer = (uint8*)*destination_shared_memory;

	if (bytes_to_copy == 0) {
		// Nothing to copy.
		return StorageDevice::ReadResponse();
	}

	if (device_offset_start + bytes_to_copy > device_->size_in_bytes) {
		// Reading beyond end of the device.
		return ::perception::Status::OVERFLOW;

	}

	if (buffer_offset_start + bytes_to_copy > destination_shared_memory.GetSize()) {
		// Writing beyond the end of the buffer.
		return ::perception::Status::OVERFLOW;
	}

	std::lock_guard<std::mutex> mutex(GetIdeMutex());
	
	int64 buffer_offset = buffer_offset_start - device_offset_start;

	/* select drive - master/slave */
	uint16 bus = device_->primary_channel ? ATA_BUS_PRIMARY : ATA_BUS_SECONDARY;
	Write8BitsToPort(ATA_DRIVE_SELECT(bus),(!device_->master_drive) << 4);
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
			SleepForDuration(std::chrono::milliseconds(10));

		while(!((status = Read8BitsFromPort(ATA_COMMAND(bus))) & 0x8) && !(status & 0x1))
			SleepForDuration(std::chrono::milliseconds(10));

		/* is there an error ? */
		if(status & 0x1) {
			/* no disk */
			return ::perception::Status::MISSING_MEDIA;
		}

		/* send the atapi packet - must be 6 words (12 bytes) long */
		uint8 atapi_packet[12] = {0xA8, 0,
			uint8((lba >> 0x18) & 0xFF),
			uint8((lba >> 0x10) & 0xFF),
			uint8((lba >> 0x08) & 0xFF),
			uint8((lba >> 0x00) & 0xFF), 0, 0, 0, 1, 0, 0};

		ResetInterrupt(device_->primary_channel);

		for(status = 0; status < 12; status += 2) {
			Write16BitsToPort(ATA_DATA(bus),*(uint16 *)&atapi_packet[status]);
		}

		WaitForInterrupt(device_->primary_channel);

		/* read in the data */
		size_t indx = lba * ATAPI_SECTOR_SIZE;
		for(size_t i = 0; i < ATAPI_SECTOR_SIZE; i+=2, indx+=2) {
			uint16 b = Read16BitsFromPort(ATA_DATA(bus));

			if(indx == device_offset_start + bytes_to_copy - 1) {
				// Copy just the last byte.
				destination_buffer[indx + buffer_offset] = (b > 8) & 0xFF;
			} else if (indx + 1 == device_offset_start) {
				// Copy just the first byte.
				destination_buffer[indx + 1 + buffer_offset_start] = b & 0xFF;
			} else if(indx >= device_offset_start &&
				indx < device_offset_start + bytes_to_copy) {
				// Copy both bytes.
				*(uint16 *)&destination_buffer[indx + buffer_offset] = b;
			}
		}
	}

	return StorageDevice::ReadResponse();
}