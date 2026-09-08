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

#include "perception/disk/mbr.h"

#include <algorithm>
#include <cstring>

namespace {

// Byte offset of partition table in MBR sector.
constexpr size_t kMbrPartitionTableOffset = 446;

// Total primary partition entries in MBR.
constexpr int kMbrEntryCount = 4;

// Size in bytes of an MBR partition entry.
constexpr size_t kMbrEntrySize = 16;

// MBR signature byte 0 at offset 510.
constexpr uint8_t kMbrSignature0 = 0x55;

// MBR signature byte 1 at offset 511.
constexpr uint8_t kMbrSignature1 = 0xAA;

// MBR partition type representing GPT Protective MBR.
constexpr uint8_t kMbrTypeGptProtective = 0xEE;

// Default standard alignment offset in sectors (1 MiB alignment for 512-byte
// sectors).
constexpr uint64_t kDefaultAlignmentSectors = 2048;

}  // namespace

namespace perception {
namespace disk {

bool DetectMbr(const uint8_t* sector0, uint64_t total_sectors) {
  if (sector0[510] != kMbrSignature0 || sector0[511] != kMbrSignature1)
    return false;

  if (std::memcmp(&sector0[3], "EXFAT   ", 8) == 0) return false;

  const MbrPartitionEntry* entries = reinterpret_cast<const MbrPartitionEntry*>(
      &sector0[kMbrPartitionTableOffset]);

  if (entries[0].partition_type == kMbrTypeGptProtective) return false;

  bool has_valid_partition = false;
  for (int i = 0; i < kMbrEntryCount; i++) {
    if (entries[i].partition_type != 0 && entries[i].sector_count > 0) {
      if (static_cast<uint64_t>(entries[i].starting_lba) +
              entries[i].sector_count <=
          total_sectors + kDefaultAlignmentSectors)
        has_valid_partition = true;
    }
  }

  if (has_valid_partition) return true;

  bool all_zero = true;
  for (size_t i = 0; i < kMbrPartitionTableOffset; i++) {
    if (sector0[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    for (int i = 0; i < kMbrEntryCount; i++) {
      if (entries[i].partition_type != 0 || entries[i].sector_count != 0 ||
          entries[i].starting_lba != 0) {
        all_zero = false;
        break;
      }
    }
    if (all_zero) return true;
  }

  return false;
}

void ParseMbrPartitions(const uint8_t* sector0, uint64_t total_sectors,
                        uint32_t sector_size,
                        std::vector<PartitionInfo>& out_partitions,
                        std::vector<FreeSpaceRange>& out_free_ranges) {
  out_partitions.clear();
  out_free_ranges.clear();

  const MbrPartitionEntry* entries = reinterpret_cast<const MbrPartitionEntry*>(
      &sector0[kMbrPartitionTableOffset]);

  struct Extent {
    uint64_t start;
    uint64_t end;
  };
  std::vector<Extent> extents;

  for (int i = 0; i < kMbrEntryCount; i++) {
    if (entries[i].partition_type == 0 || entries[i].sector_count == 0)
      continue;

    PartitionInfo info;
    info.partition_number = i + 1;
    info.mbr_type = entries[i].partition_type;
    info.type_name = GetMbrTypeName(entries[i].partition_type);
    info.name = "Partition " + std::to_string(i + 1);
    info.start_lba = entries[i].starting_lba;
    info.sector_count = entries[i].sector_count;
    info.end_lba = info.start_lba + info.sector_count - 1;
    info.size_in_bytes = info.sector_count * sector_size;

    out_partitions.push_back(info);
    extents.push_back({info.start_lba, info.end_lba});
  }

  std::sort(extents.begin(), extents.end(),
            [](const Extent& a, const Extent& b) { return a.start < b.start; });

  uint64_t current_lba = kDefaultAlignmentSectors;
  for (const auto& ext : extents) {
    if (ext.start > current_lba) {
      FreeSpaceRange range;
      range.start_lba = current_lba;
      range.end_lba = ext.start - 1;
      range.sector_count = range.end_lba - range.start_lba + 1;
      range.size_in_bytes = range.sector_count * sector_size;
      if (range.sector_count >= kDefaultAlignmentSectors)
        out_free_ranges.push_back(range);
    }
    if (ext.end + 1 > current_lba) current_lba = ext.end + 1;
  }

  if (current_lba < total_sectors) {
    FreeSpaceRange range;
    range.start_lba = current_lba;
    range.end_lba = total_sectors - 1;
    range.sector_count = range.end_lba - range.start_lba + 1;
    range.size_in_bytes = range.sector_count * sector_size;
    if (range.sector_count > 0) out_free_ranges.push_back(range);
  }
}

std::vector<uint8_t> CreateEmptyMbrSector() {
  std::vector<uint8_t> sector(512, 0);
  sector[510] = kMbrSignature0;
  sector[511] = kMbrSignature1;
  return sector;
}

bool AddMbrPartitionToSector(uint8_t* sector0, uint64_t start_lba,
                             uint64_t sector_count, uint8_t partition_type) {
  MbrPartitionEntry* entries =
      reinterpret_cast<MbrPartitionEntry*>(&sector0[kMbrPartitionTableOffset]);

  for (int i = 0; i < kMbrEntryCount; i++) {
    if (entries[i].partition_type == 0 || entries[i].sector_count == 0) {
      std::memset(&entries[i], 0, sizeof(MbrPartitionEntry));
      entries[i].boot_indicator = 0x00;
      entries[i].partition_type = partition_type;
      entries[i].starting_lba = static_cast<uint32_t>(start_lba);
      entries[i].sector_count = static_cast<uint32_t>(sector_count);
      sector0[510] = kMbrSignature0;
      sector0[511] = kMbrSignature1;
      return true;
    }
  }
  return false;
}

bool DeleteMbrPartitionFromSector(uint8_t* sector0, int partition_index) {
  if (partition_index < 0 || partition_index >= kMbrEntryCount) return false;

  MbrPartitionEntry* entries =
      reinterpret_cast<MbrPartitionEntry*>(&sector0[kMbrPartitionTableOffset]);
  std::memset(&entries[partition_index], 0, sizeof(MbrPartitionEntry));
  return true;
}

std::string GetMbrTypeName(uint8_t type) {
  switch (type) {
    case 0x07:
      return "exFAT / NTFS";
    case 0x0B:
      return "FAT32 CHS";
    case 0x0C:
      return "FAT32 LBA";
    case 0x83:
      return "Linux Native";
    case 0xEE:
      return "GPT Protective";
    case 0xEF:
      return "EFI System";
    default: {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "Type 0x%02X", type);
      return std::string(buf);
    }
  }
}

}  // namespace disk
}  // namespace perception
