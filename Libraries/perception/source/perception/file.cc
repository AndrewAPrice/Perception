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

#include <cmath>
#include <cstdio>

#include "perception/serialization/serializer.h"

namespace perception {
namespace {

// Available size units starting from kilobytes.
constexpr const char* kUnits[] = {"KB", "MB", "GB", "TB", "PB", "EB"};

// Number of available size units.
constexpr size_t kNumUnits = sizeof(kUnits) / sizeof(kUnits[0]);

}  // namespace

std::string FormatSize(uint64 bytes) {
  if (bytes < 1024) return std::to_string(bytes) + " B";

  double value = static_cast<double>(bytes) / 1024.0;
  size_t unit_index = 0;
  while (unit_index + 1 < kNumUnits &&
         std::round(value * 100.0) / 100.0 >= 1024.0) {
    value /= 1024.0;
    unit_index++;
  }

  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, kUnits[unit_index]);
  return buffer;
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
