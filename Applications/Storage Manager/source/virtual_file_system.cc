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

using ::file_systems::FileSystem;
using ::permebuf::perception::devices::StorageType;
using ::permebuf::perception::DirectoryEntryType;

namespace {

std::map<std::string, std::unique_ptr<FileSystem>, std::less<>> mounted_file_systems;

int next_optical_drive_index = 0;
int next_unknown_device_index = 0;

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

void ForEachEntryInDirectory(std::string_view directory,
	int offset, int count, const std::function<void(std::string_view,
		DirectoryEntryType, size_t)>& on_each_entry) {
	if (directory.empty() || directory[0] != '/')
		return;

	if (directory == "/") {
		int index = 0;
		// Iterating the root directory, return each mount point.
		for (const auto& mounted_file_system : mounted_file_systems) {
			if (index >= offset && (index < count || count == 0)) {
				on_each_entry(mounted_file_system.first,
					DirectoryEntryType::Directory, 0);
			}
			index++;
		}
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
			return;  // No.

		// Scan the directory within the file system.
		mount_point_itr->second->ForEachEntryInDirectory(
			directory, offset, count, on_each_entry);
	}
}

