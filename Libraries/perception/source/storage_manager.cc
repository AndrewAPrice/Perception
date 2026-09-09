// Copyright 2025 Google LLC
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
#include "perception/storage_manager.h"

#include "perception/serialization/serializer.h"

namespace perception {

void DirectoryEntry::Serialize(serialization::Serializer& serializer) {
  serializer.String("Name", name);
  serializer.Integer("Type", type);
  serializer.Integer("Size in bytes", size_in_bytes);
  serializer.Integer("Is link", is_link);
}

void RequestWithFilePath::Serialize(serialization::Serializer& serializer) {
  serializer.String("Path", path);
  serializer.Integer("No follow", no_follow);
}

void OpenFileResponse::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("File", file);
  serializer.Integer("Size in bytes", size_in_bytes);
  serializer.Integer("Optimal operation size", optimal_operation_size);
}

void OpenMemoryMappedFileResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.Serializable("File", file);
  serializer.Serializable("File contents", file_contents);
}

void ReadDirectoryRequest::Serialize(serialization::Serializer& serializer) {
  serializer.String("Path", path);
  serializer.Integer("First index", first_index);
  serializer.Integer("Maximum number of entries", maximum_number_of_entries);
}

void ReadDirectoryResponse::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Entries", entries);
  serializer.Integer("Has more entries", has_more_entries);
}

void CheckPermissionsResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.Integer("Exists", exists);
  serializer.Integer("Can read", can_read);
  serializer.Integer("Can write", can_write);
  serializer.Integer("Can execute", can_execute);
}

void FileStatistics::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Exists", exists);
  serializer.Integer("Type", type);
  serializer.Integer("Size in bytes", size_in_bytes);
  serializer.Integer("Optimal operation size", optimal_operation_size);
  serializer.Integer("Is link", is_link);
}

void OpenFileRequest::Serialize(serialization::Serializer& serializer) {
  serializer.String("Path", path);
  serializer.Integer("Read access", read_access);
  serializer.Integer("Write access", write_access);
  serializer.Integer("Create if not exists", create_if_not_exists);
  serializer.Integer("Truncate", truncate);
}

void FileSystemMountEvent::Serialize(serialization::Serializer& serializer) {
  serializer.String("mount_point", mount_point);
}

void MountedFileSystemDetails::Serialize(
    serialization::Serializer& serializer) {
  serializer.String("mount_point", mount_point);
  serializer.Serializable("device", device);
  serializer.Integer("start_byte_offset", start_byte_offset);
  serializer.Integer("byte_length", byte_length);
  serializer.String("device_name", device_name);
  serializer.String("filesystem_type", filesystem_type);
  serializer.Integer("is_writable", is_writable);
  serializer.Integer("is_boot_drive", is_boot_drive);
}

void GetMountedFileSystemsResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.ArrayOfStrings("mount_points", mount_points);
  serializer.ArrayOfSerializables("file_systems", file_systems);
}

void SetMountPathRequest::Serialize(serialization::Serializer& serializer) {
  serializer.String("old_mount_point", old_mount_point);
  serializer.String("new_mount_point", new_mount_point);
}

void MountFileSystemRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("device", device);
  serializer.Integer("start_byte_offset", start_byte_offset);
  serializer.Integer("byte_length", byte_length);
  serializer.String("target_mount_point", target_mount_point);
}

void MountFileSystemResponse::Serialize(serialization::Serializer& serializer) {
  serializer.String("mount_point", mount_point);
}

}  // namespace perception