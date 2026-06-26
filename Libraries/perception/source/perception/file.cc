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
#include "perception/file.h"

#include "perception/serialization/serializer.h"

namespace perception {

std::string FormatSize(uint64 bytes) {
  if (bytes < 1024) return std::to_string(bytes) + " B";

  uint64 kb = bytes / 1024;
  if (kb < 1024) return std::to_string(kb) + " KB";

  uint64 mb = kb / 1024;
  if (mb < 1024) return std::to_string(mb) + " MB";

  uint64 gb = mb / 1024;
  if (gb < 1024) return std::to_string(gb) + " GB";

  uint64 tb = gb / 1024;
  if (tb < 1024) return std::to_string(tb) + " TB";

  uint64 pb = tb / 1024;
  if (pb < 1024) return std::to_string(pb) + " PB";

  uint64 eb = pb / 1024;
  return std::to_string(eb) + " EB";
}

void ReadFileRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Offset in file", offset_in_file);
  serializer.Integer("Offset in destination buffer",
                     offset_in_destination_buffer);
  serializer.Integer("Bytes to copy", bytes_to_copy);
  serializer.Serializable("Buffer to copy into", buffer_to_copy_into);
}

void WriteFileRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Offset in file", offset_in_file);
  serializer.Integer("Bytes to copy", bytes_to_copy);
  serializer.Serializable("Buffer to copy from", buffer_to_copy_from);
}

void GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest::Serialize(
    serialization::Serializer& serializer) {
  serializer.Serializable("Buffer", buffer);
}

}  // namespace perception
