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

#include "virtual_file_system.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>

using ::file_systems::FileSystem;
using ::perception::Status;
using ::perception::ProcessId;
using ::permebuf::perception::devices::StorageType;
using ::permebuf::perception::DirectoryEntryType;
using ::permebuf::perception::File;

namespace {

std::map<std::string, std::unique_ptr<FileSystem>, std::less<>> mounted_file_systems;

std::map<ProcessId, std::vector<std::unique_ptr<File::Server>>> open_files_by_process_id;

int next_optical_drive_index = 1;
int next_unknown_device_index = 1;

std::string GetMountNameForFileSystem(FileSystem& file_system) {
	switch (file_system.GetStorageType()) {
		case StorageType::Optical: {
			std::string name = "Optical " + std::to_string(next_optical_drive_index);
			next_optical_drive_index++;
			return name;
		}
		default: {
			std::string name = std::to_string(next_unknown_device_index);
			next_unknown_device_index++;
			return name;
		}
	}
}

}

void MountFileSystem(std::unique_ptr<FileSystem> file_system) {
	std::string mount_name = GetMountNameForFileSystem(*file_system);
	std::cout << "Mounting " << file_system->GetFileSystemType() <<
				" on " << file_system->GetDeviceName() <<
				" as /" << mount_name << "/" << std::endl;
	mounted_file_systems[mount_name] = std::move(file_system);
}


StatusOr<::permebuf::perception::File::Server*> OpenFile(
	std::string_view path,
	size_t& size_in_bytes,
	ProcessId sender) {
	if (path.empty() || path[0] != '/')
		return Status::FILE_NOT_FOUND;

	// Jump over the initial '/'.
	path = path.substr(1);

	// Find the split point (/) between the mount path and everything else.
	int split_point = path.find_first_of('/');

	if (split_point == std::string_view::npos)
		return Status::FILE_NOT_FOUND;

	std::string_view mount_point = path.substr(0, split_point);
	path = path.substr(split_point + 1);

	// Does the mount point exist?
	auto mount_point_itr = mounted_file_systems.find(mount_point);
	if (mount_point_itr == mounted_file_systems.end())
		return Status::FILE_NOT_FOUND;  // No mount point.

	// Scan the directory within the file system.
	ASSIGN_OR_RETURN(auto file, mount_point_itr->second->OpenFile(
		path, size_in_bytes, sender));
	File::Server* file_ptr = file.get();

	auto itr = open_files_by_process_id.find(sender);
	if (itr == open_files_by_process_id.end()) {
		// First file open by this process.
		std::vector<std::unique_ptr<File::Server>> files;
		files.push_back(std::move(file));
		open_files_by_process_id[sender] = std::move(files);
	} else {
		itr->second.push_back(std::move(file));
	}

	return file_ptr;
}


void CloseFile(::perception::ProcessId sender,
	::permebuf::perception::File::Server* file) {
	std::cout << "TODO: Implement CloseFile" << std::endl;
}

bool ForEachEntryInDirectory(std::string_view directory,
	int offset, int count, const std::function<void(std::string_view,
		DirectoryEntryType, size_t)>& on_each_entry) {
	if (directory.empty() || directory[0] != '/')
		return true;

	if (directory == "/") {
		int index = 0;
		// Iterating the root directory, return each mount point.
		for (const auto& mounted_file_system : mounted_file_systems) {
			if (count != 0 && index >= offset + count) {
				// We are terminating early, but there is still more to
				// iterate.
				return false;
			}
			if (index >= offset && (index < offset + count || count == 0)) {
				on_each_entry(mounted_file_system.first,
					DirectoryEntryType::Directory, 0);
			}
			index++;
		}
		return true;  // Nothing more to iterate.
	} else {
		// Split the path into the mount path and everything else.

		// Jump over initial '/'.
		directory = directory.substr(1);

		// Trim off the last '/'.
		if (directory[directory.size() - 1] == '/')
			directory = directory.substr(0, directory.size() - 1);

		// Find the split point (/) between the mount path and everything else.
		int split_point = directory.find_first_of('/');

		std::string_view mount_point;
		if (split_point == std::string_view::npos) {
			// Root directory in the mount point.
			mount_point = directory;
			directory = "";
		} else {
			mount_point = directory.substr(0, split_point);
			directory = directory.substr(split_point + 1);
		}

		// Does the mount point exist?
		auto mount_point_itr = mounted_file_systems.find(mount_point);
		if (mount_point_itr == mounted_file_systems.end())
			return true;  // No mount point.

		// Scan the directory within the file system.
		return mount_point_itr->second->ForEachEntryInDirectory(
			directory, offset, count, on_each_entry);
	}
}

