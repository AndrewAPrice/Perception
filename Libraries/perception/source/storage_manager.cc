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
}

void RequestWithFilePath::Serialize(serialization::Serializer& serializer) {
  serializer.String("Path", path);
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
}

}  // namespace perception