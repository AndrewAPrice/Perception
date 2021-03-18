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

#include <functional>
#include <memory>
#include <string_view>

#include "permebuf/Libraries/perception/devices/storage_device.permebuf.h"

class DirectoryEntry;
class File;

namespace file_systems {

class FileSystem {
public:
	virtual ~FileSystem() {}

	// Opens a file.
	virtual std::unique_ptr<File> OpenFile(std::string_view path) = 0;

	// Counts the number of entries in a directory.
	virtual size_t CountEntriesInDirectory(std::string_view path) = 0;

	// If count is 0, then we will iterate over all of the entries in
	// a directory.
	virtual void ForEachEntryInDirectory(std::string_view path,
		size_t start_index, size_t count,
		const std::function<void(const DirectoryEntry&)>& on_each_entry) = 0;
};


// Returns a FileSystem instance for accessing this storage device if it's
// a file system we can handle, otherwise returns a nullptr.
static std::unique_ptr<FileSystem> InitializeStorageDevice(
	::permebuf::perception::devices::StorageDevice storage_device);

}