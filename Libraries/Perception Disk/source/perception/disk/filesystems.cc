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

#include "perception/disk/filesystems.h"

#include <cctype>

namespace {

// Standard exFAT display name.
constexpr std::string_view kExfatName = "exFAT";

// Standard ISO 9660 display name.
constexpr std::string_view kIso9660Name = "ISO 9660";

// Standard Raw display name.
constexpr std::string_view kRawName = "Raw";

// Standard Unknown display name.
constexpr std::string_view kUnknownName = "Unknown";

bool EqualsIgnoreCaseAndWhitespace(std::string_view a, std::string_view b) {
  size_t i = 0;
  size_t j = 0;
  while (i < a.size() || j < b.size()) {
    while (i < a.size() && (a[i] == ' ' || a[i] == '-' || a[i] == '_')) i++;
    while (j < b.size() && (b[j] == ' ' || b[j] == '-' || b[j] == '_')) j++;
    if (i == a.size() || j == b.size()) return i == a.size() && j == b.size();
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[j])))
      return false;
    i++;
    j++;
  }
  return true;
}

}  // namespace

namespace perception {
namespace disk {

std::string_view FilesystemTypeToString(FilesystemType type) {
  switch (type) {
    case FilesystemType::EXFAT:
      return kExfatName;
    case FilesystemType::ISO9660:
      return kIso9660Name;
    case FilesystemType::RAW:
      return kRawName;
    case FilesystemType::UNKNOWN:
    default:
      return kUnknownName;
  }
}

FilesystemType FilesystemTypeFromString(std::string_view name) {
  if (EqualsIgnoreCaseAndWhitespace(name, "exfat"))
    return FilesystemType::EXFAT;
  if (EqualsIgnoreCaseAndWhitespace(name, "iso9660") ||
      EqualsIgnoreCaseAndWhitespace(name, "iso"))
    return FilesystemType::ISO9660;
  if (EqualsIgnoreCaseAndWhitespace(name, "raw") ||
      EqualsIgnoreCaseAndWhitespace(name, "unformatted"))
    return FilesystemType::RAW;

  return FilesystemType::UNKNOWN;
}

const std::vector<FilesystemType>& GetSupportedFilesystems() {
  static const std::vector<FilesystemType> supported_filesystems = {
      FilesystemType::EXFAT, FilesystemType::ISO9660};
  return supported_filesystems;
}

const std::vector<FilesystemType>& GetWritableFilesystems() {
  static const std::vector<FilesystemType> writable_filesystems = {
      FilesystemType::EXFAT};
  return writable_filesystems;
}

bool IsFilesystemWritable(FilesystemType type) {
  return type == FilesystemType::EXFAT;
}

}  // namespace disk
}  // namespace perception
