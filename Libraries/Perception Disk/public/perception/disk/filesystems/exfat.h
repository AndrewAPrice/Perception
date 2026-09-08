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
#include <string>

#include "perception/disk/filesystem_analyzer.h"
#include "perception/disk/formatter.h"

namespace perception {
namespace disk {
namespace filesystems {

// Formats an exFAT filesystem across the specified LBA range.
bool FormatExfat(uint64_t start_lba, uint64_t sector_count,
                 uint32_t sector_size, const std::string& volume_label,
                 const BlockWriter& writer);

// Updates the partition offset in an existing exFAT boot region and recomputes checksums.
bool UpdateExfatPartitionOffset(uint64_t new_start_lba, uint32_t sector_size,
                                const BlockReader& reader,
                                const BlockWriter& writer);

}  // namespace filesystems
}  // namespace disk
}  // namespace perception

