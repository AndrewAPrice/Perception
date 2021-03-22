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

#include <iostream>

#include "permebuf/Libraries/perception/storage_manager.permebuf.h"
#include "shared_memory_pool.h"

// using ::permebuf::perception::File;
using ::permebuf::perception::devices::StorageDevice;
using ::permebuf::perception::DirectoryEntryType;
using ::perception::ProcessId;

namespace file_systems {
namespace {

constexpr int kIso9660SectorSize = 2048;
std::string kIso9660Name = "ISO 9660";

/*
class Iso9660File : public File {
public:
	Iso9660File(
		StorageDevice storage_device,
		size_t offset_on_device,
		size_t length_of_file,
		ProcessId allowed_process) :
		storage_device_(storage_device),
		offset_on_device_(offset_on_device),
		length_of_file_(length_of_file),
		allowed_process_(allowed_process),
		open_(true) {};

	virtual void HandleCloseFile(ProcessId sender,
		const File::CloseFileMessage&) override {
		if (sender != allowed_process_)
			return;

		std::cout << "Implement Iso9660File::HandleCloseFile" << std::endl;
	}

	virtual StatusOr<File::ReadFileResponse>
		HandleReadFile(ProcessId sender,
			const File::ReadFileRequest& request) override {
		if (sender != allowed_process_)
			return ::perception::Status::NOT_ALLOWED;

		if (request.GetOffsetInFile() + request.GetBytesToCopy() > length_of_file_) {
			return ::perception::Status::OVERFLOW;
		}

		StorageDevice::ReadRequest read_request;
		read_request.SetOffsetOnDevice(
			offset_on_device_ + request.GetOffsetInFile());
		read_request.SetOffsetInBuffer(request.GetOffsetInDestinationBuffer());
		read_request.SetBytesToCopy(request.GetBytesToCopy());
		read_request.SetBuffer(request.GetBufferToCopyInto());

		RETURN_IF_ERROR(storage_device_.CallRead(read_request));		

		return File::ReadFileResponse();
	}

private:
	::permebuf::perception::devices::StorageDevice storage_device_;
	size_t offset_on_device_;
	size_t length_of_file_;
	ProcessId allowed_process_;
	bool open_;
};
*/
}


Iso9660::Iso9660(uint32 size_in_blocks, uint16 logical_block_size,
		std::unique_ptr<char[]> root_directory, StorageDevice storage_device) :
	size_in_blocks_(size_in_blocks), logical_block_size_(logical_block_size),
	root_directory_(std::move(root_directory)), FileSystem(storage_device) {}

// Opens a file.
std::unique_ptr<File> Iso9660::OpenFile(std::string_view path, ProcessId process) {
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
	const std::function<void(std::string_view,
			DirectoryEntryType, size_t)>& on_each_entry) {
	while (!path.empty()) {
		// Strip out any remaining slashes.
		while (!path.empty() && )

	}

}

std::string_view Iso9660::GetFileSystemType() {
	return kIso9660Name;
}
