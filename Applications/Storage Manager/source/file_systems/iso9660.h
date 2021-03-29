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

#pragma once

#include <memory>

#include "file_systems/file_system.h"

namespace file_systems {

class Iso9660 : public FileSystem {
public:
	Iso9660(uint32 size_in_blocks, uint16 logical_block_size,
		std::unique_ptr<char[]> root_directory,
		::permebuf::perception::devices::StorageDevice storage_device);

	virtual ~Iso9660() {}

	virtual std::string_view GetFileSystemType() override;

	// Opens a file.
	virtual StatusOr<std::unique_ptr<::permebuf::perception::File::Server>>
		OpenFile(std::string_view path, size_t size_in_bytes,
			::perception::ProcessId sender) override;

	// Counts the number of entries in a directory.
	virtual size_t CountEntriesInDirectory(std::string_view path) override;

	// If count is 0, then we will iterate over all of the entries in
	// a directory. Returns if we have no more files in this directory
	// to iterate over, otherwise returns false if we aborted early
	// because we have more entries than what is in 'count'.
	virtual bool ForEachEntryInDirectory(std::string_view path,
		size_t start_index, size_t count,
		const std::function<void(std::string_view,
			::permebuf::perception::DirectoryEntryType,
			size_t)>& on_each_entry) override;

private:
	// Size of the volume, in logical blocks.
	uint32 size_in_blocks_;

	// Logical block size, in bytes.
	uint16 logical_block_size_;

	// Root directory entry.
	std::unique_ptr<char[]> root_directory_;


	void ForRawEachEntryInDirectory(std::string_view path,
		const std::function<bool(std::string_view,
			::permebuf::perception::DirectoryEntryType,
			size_t, size_t)>& on_each_entry);
};

// Returns a FileSystem instance if this device is in the Iso 9660 format.
std::unique_ptr<FileSystem> InitializeIso9960ForStorageDevice(
	::permebuf::perception::devices::StorageDevice storage_device);

}
