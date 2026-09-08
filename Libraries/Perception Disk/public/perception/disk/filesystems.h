// Copyright 2026 Google LLC
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

#include <string_view>
#include <vector>

namespace perception {
namespace disk {

// Recognized filesystem types supported by Perception OS virtual file system.
enum class FilesystemType { UNKNOWN, EXFAT, ISO9660, RAW };

// Converts a filesystem type enum to its display name string.
std::string_view FilesystemTypeToString(FilesystemType type);

// Converts a filesystem name string to its corresponding enum value.
FilesystemType FilesystemTypeFromString(std::string_view name);

// Returns list of supported filesystem types in Perception OS.
const std::vector<FilesystemType>& GetSupportedFilesystems();

// Returns list of writable (formattable) filesystem types in Perception OS.
const std::vector<FilesystemType>& GetWritableFilesystems();

// Returns whether the specified filesystem type is writable / formattable.
bool IsFilesystemWritable(FilesystemType type);

}  // namespace disk
}  // namespace perception
