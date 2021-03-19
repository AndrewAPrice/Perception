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

#include <map>
#include <string>

using ::file_systems::FileSystem;
using ::permebuf::perception::devices::StorageType;

namespace {

std::map<std::string, std::unique_ptr<FileSystem>> mounted_file_systems;

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
	mounted_file_systems[mount_name] == file_system;
}
