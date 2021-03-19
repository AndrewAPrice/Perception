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

#include "file_systems/iso9660.h"

#include "shared_memory_pool.h"

using ::permebuf::perception::devices::StorageDevice;
using ::permebuf::perception::devices::StorageDeviceStatus;

namespace file_systems {
namespace {

constexpr int kIso9660SectorSize = 2048;
std::string kIso9660Name = "ISO 9660";

}


Iso9660::Iso9660(uint32 size_in_blocks, uint16 logical_block_size,
		std::unique_ptr<char[]> root_directory, StorageDevice storage_device) :
	size_in_blocks_(size_in_blocks), logical_block_size_(logical_block_size),
	root_directory_(std::move(root_directory)), FileSystem(storage_device) {}

// Opens a file.
std::unique_ptr<File> Iso9660::OpenFile(std::string_view path) {
	return std::unique_ptr<File>();
}

// Counts the number of entries in a directory.
size_t Iso9660::CountEntriesInDirectory(std::string_view path) {
	return 0;
}

// If count is 0, then we will iterate over all of the entries in
// a directory.
void Iso9660::ForEachEntryInDirectory(std::string_view path,
	size_t start_index, size_t count,
	const std::function<void(const DirectoryEntry&)>& on_each_entry) {
}

std::string_view Iso9660::GetFileSystemType() {
	return kIso9660Name;
}

std::unique_ptr<FileSystem> InitializeIso9960ForStorageDevice(
		StorageDevice storage_device) {
	auto status_or_device_details = storage_device.CallGetDeviceDetails(
		StorageDevice::GetDeviceDetailsRequest());
	std::string_view device_name = *(*status_or_device_details)->GetName();

	auto pooled_shared_memory = GetSharedMemory();
	char* buffer = (char*)**pooled_shared_memory->shared_memory;

	StorageDevice::ReadRequest read_request;
	read_request.SetOffsetOnDevice(0);
	read_request.SetOffsetInBuffer(0);
	read_request.SetBytesToCopy(kIso9660SectorSize);
	read_request.SetBuffer(*pooled_shared_memory->shared_memory);

	// Start at sector 0x10 and keep looping until we run out of space,
	// stop finding volume descriptors, or find the primary volume descriptor.
	uint64 sector = 0x10;
	while (true) {
		// Read in this sector.
		read_request.SetOffsetOnDevice(sector * kIso9660SectorSize);
		auto status_or_response = storage_device.CallRead(read_request);
		if (!status_or_response || status_or_response->GetStatus() !=
			StorageDeviceStatus::Successful) {
			// Probably ran past the end of the disk.
			ReleaseSharedMemory(std::move(pooled_shared_memory));
			return std::unique_ptr<FileSystem>(); 
		}

		// Are bytes CD001?
		if (buffer[1] != 'C' || buffer[2] != 'D' || buffer[3] != '0' ||
			buffer[4] != '0' || buffer[5] != '1') {
			// No more volume descriptors.
			ReleaseSharedMemory(std::move(pooled_shared_memory));
			return std::unique_ptr<FileSystem>(); 
		}

		if (buffer[0] == 1) {
			// This is a primary volume descriptor.
			break;
		} else {
			// Jump to the next sector.
			sector++;
		}
	}

	if (buffer[6] != 0x01) {
		std::cout << "Unknown ISO 9660 Version number on " <<
			device_name << std::endl;
		ReleaseSharedMemory(std::move(pooled_shared_memory));
		return std::unique_ptr<FileSystem>();
	} // Check version

	if (*(uint16 *)&buffer[120] != 1) {
		std::cout << "We only support single set ISO 9660 disks on " <<
			device_name << std::endl;
		ReleaseSharedMemory(std::move(pooled_shared_memory));
		return std::unique_ptr<FileSystem>();
	} 

	if (buffer[881] != 0x01) {
		std::cout << "Unsupported ISO 9660 directory records and path table on " <<
			device_name << std::endl;
		ReleaseSharedMemory(std::move(pooled_shared_memory));
		return std::unique_ptr<FileSystem>(); 
	}

	uint32 size_in_blocks = *(uint32 *)&buffer[80];
	uint16 logical_block_size = *(uint16 *)&buffer[128];

	// Copy root directory entry.
	auto root_directory = std::make_unique<char[]>(34);
	memcpy(root_directory.get(), &buffer[156], 34);

	ReleaseSharedMemory(std::move(pooled_shared_memory));
	return std::unique_ptr<Iso9660>(new Iso9660(size_in_blocks,
		logical_block_size, std::move(root_directory),
		storage_device));
}

}
