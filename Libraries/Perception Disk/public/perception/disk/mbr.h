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
#include <vector>

#include "perception/disk/disk_structures.h"

namespace perception {
namespace disk {

// Master Boot Record partition entry structure.
struct MbrPartitionEntry {
  // Boot indicator (0x80 = active, 0x00 = inactive).
  uint8_t boot_indicator;

  // Starting cylinder-head-sector address.
  uint8_t starting_chs[3];

  // Partition type identifier.
  uint8_t partition_type;

  // Ending cylinder-head-sector address.
  uint8_t ending_chs[3];

  // Starting logical block address.
  uint32_t starting_lba;

  // Total sector count in the partition.
  uint32_t sector_count;
};

// Returns true if sector 0 contains a valid MBR partition table (and is not a GPT protective MBR).
bool DetectMbr(const uint8_t* sector0, uint64_t total_sectors);

// Parses partition entries and free space ranges from sector 0.
void ParseMbrPartitions(const uint8_t* sector0, uint64_t total_sectors,
                        uint32_t sector_size,
                        std::vector<PartitionInfo>& out_partitions,
                        std::vector<FreeSpaceRange>& out_free_ranges);

// Generates an initialized MBR sector 0 buffer with an empty partition table.
std::vector<uint8_t> CreateEmptyMbrSector();

// Adds a new partition entry to the MBR sector.
bool AddMbrPartitionToSector(uint8_t* sector0, uint64_t start_lba,
                             uint64_t sector_count, uint8_t partition_type);

// Removes a partition entry by index (0-3) from the MBR sector.
bool DeleteMbrPartitionFromSector(uint8_t* sector0, int partition_index);

// Returns a human-readable name for a given MBR partition type byte.
std::string GetMbrTypeName(uint8_t type);

}  // namespace disk
}  // namespace perception
