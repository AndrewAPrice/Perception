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

#include <cstdint>
#include <functional>
#include <string>

#include "perception/disk/filesystems.h"

namespace perception {
namespace disk {

// Callback type for writing raw bytes to a device at a specific offset.
using BlockWriter =
    std::function<bool(uint64_t offset, size_t bytes, const void* src)>;

// Formats a filesystem across the specified LBA range.
bool FormatFilesystem(FilesystemType type, uint64_t start_lba,
                      uint64_t sector_count, uint32_t sector_size,
                      const std::string& volume_label,
                      const BlockWriter& writer);

}  // namespace disk
}  // namespace perception
