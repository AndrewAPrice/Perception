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

#include "perception/disk/disk_structures.h"

namespace perception {
namespace disk {

// Callback type for reading raw bytes from a device at a specific offset.
using BlockReader =
    std::function<bool(uint64_t offset, size_t bytes, void* dest)>;

// Analyzes the filesystem on a partition or device range, calculating used and free space.
void AnalyzeFilesystem(uint64_t start_lba, uint64_t sector_count,
                       uint32_t sector_size, const BlockReader& reader,
                       PartitionInfo& partition);

// Analyzes a whole raw storage device (with no partition table) for filesystem and free space.
void AnalyzeRawDevice(uint64_t total_sectors, uint32_t sector_size,
                      const BlockReader& reader, DiskInfo& disk);

}  // namespace disk
}  // namespace perception
