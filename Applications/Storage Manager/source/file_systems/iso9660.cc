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

using ::permebuf::perception::devices::StorageDevice;

namespace file_system {

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

std::unique_ptr<FileSystem> InitializeIso9960ForStorageDevice(
		::permebuf::perception::devices::StorageDevice storage_device) {
	return std::unique_ptr<FileSystem>();
}

}
