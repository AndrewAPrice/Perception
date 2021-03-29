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

#include "storage_manager.h"

#include <iostream>

#include "virtual_file_system.h"

using ::perception::ProcessId;
using ::permebuf::perception::DirectoryEntry;
using ::permebuf::perception::DirectoryEntryType;
using ::permebuf::perception::File;
using SM = ::permebuf::perception::StorageManager;

StorageManager::StorageManager() {}

StorageManager::~StorageManager() {}

StatusOr<SM::OpenFileResponse> StorageManager::HandleOpenFile(
	::perception::ProcessId sender,
	Permebuf<SM::OpenFileRequest> request) {
	size_t size_in_bytes = 0;
	ASSIGN_OR_RETURN(File::Server* file,
		OpenFile(*request->GetPath(), size_in_bytes, sender));

	SM::OpenFileResponse response;
	response.SetFile(*file);
	response.SetSizeInBytes(size_in_bytes);
	return response;
}

StatusOr<Permebuf<SM::ReadDirectoryResponse>> StorageManager::HandleReadDirectory(
	::perception::ProcessId sender,
	Permebuf<SM::ReadDirectoryRequest> request) {
	Permebuf<SM::ReadDirectoryResponse> response;

	PermebufListOf<DirectoryEntry> last_directory_entry;

	bool no_more_entries = ForEachEntryInDirectory(*request->GetPath(),
		request->GetFirstIndex(),
		request->GetMaximumNumberOfEntries(),
	[&](std::string_view name, DirectoryEntryType type, size_t size) {
		auto directory_entry = response.AllocateMessage<DirectoryEntry>();
		directory_entry.SetName(name);
		directory_entry.SetType(type);
		directory_entry.SetSizeInBytes(size);

		if (last_directory_entry.IsValid()) {
			last_directory_entry = last_directory_entry.InsertAfter();
		}
		else {
			last_directory_entry = response->MutableEntries();
		}
		last_directory_entry.Set(directory_entry);
	});
	response->SetHasMoreEntries(!no_more_entries);
	return response;
}
