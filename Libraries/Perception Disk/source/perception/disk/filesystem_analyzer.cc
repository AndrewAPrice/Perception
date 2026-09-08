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

#include "perception/disk/filesystem_analyzer.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

// Size in bytes of a standard sector.
constexpr size_t kSectorSize = 512;

// Optical disc sector size in bytes.
constexpr size_t kOpticalSectorSize = 2048;

// Primary Volume Descriptor sector index for ISO 9660.
constexpr uint64_t kIso9660PvdSector = 16;

// exFAT allocation bitmap directory entry type identifier.
constexpr uint8_t kExfatEntryTypeAllocationBitmap = 0x81;

// exFAT volume label directory entry type identifier.
constexpr uint8_t kExfatEntryTypeVolumeLabel = 0x83;

// Checks whether a sector begins with an exFAT boot record.
bool CheckExfat(const uint8_t* boot_sector) {
  if (std::memcmp(&boot_sector[3], "EXFAT   ", 8) != 0) return false;
  if (boot_sector[510] != 0x55 || boot_sector[511] != 0xAA) return false;
  return true;
}

// Inspects an exFAT volume to determine allocation metrics and volume name.
bool AnalyzeExfatVolume(uint64_t start_offset,
                        const perception::disk::BlockReader& reader,
                        uint64_t& out_used_bytes, uint64_t& out_free_bytes,
                        std::string& out_volume_name) {
  std::vector<uint8_t> boot_sector(kSectorSize);
  if (!reader(start_offset, kSectorSize, boot_sector.data())) return false;

  if (!CheckExfat(boot_sector.data())) return false;

  uint64_t volume_length = *reinterpret_cast<const uint64_t*>(&boot_sector[72]);
  uint32_t fat_offset = *reinterpret_cast<const uint32_t*>(&boot_sector[80]);
  uint32_t fat_length = *reinterpret_cast<const uint32_t*>(&boot_sector[84]);
  uint32_t cluster_heap_offset =
      *reinterpret_cast<const uint32_t*>(&boot_sector[88]);
  uint32_t cluster_count = *reinterpret_cast<const uint32_t*>(&boot_sector[92]);
  uint32_t root_dir_cluster =
      *reinterpret_cast<const uint32_t*>(&boot_sector[96]);
  uint8_t sector_shift = boot_sector[108];
  uint8_t cluster_shift = boot_sector[109];

  if (sector_shift < 9 || sector_shift > 12) return false;

  uint32_t bytes_per_sector = 1 << sector_shift;
  uint32_t sectors_per_cluster = 1 << cluster_shift;
  uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;

  auto cluster_to_offset = [&](uint32_t cluster) -> uint64_t {
    return start_offset +
           (static_cast<uint64_t>(cluster_heap_offset) +
            static_cast<uint64_t>(cluster - 2) * sectors_per_cluster) *
               bytes_per_sector;
  };

  // Read root directory cluster to discover Allocation Bitmap and Volume Label
  uint64_t root_dir_offset = cluster_to_offset(root_dir_cluster);
  std::vector<uint8_t> root_dir_buf(cluster_size);
  if (!reader(root_dir_offset, cluster_size, root_dir_buf.data())) return false;

  uint32_t bitmap_cluster = 0;
  uint64_t bitmap_length = 0;

  for (size_t offset = 0; offset + 32 <= root_dir_buf.size(); offset += 32) {
    uint8_t entry_type = root_dir_buf[offset];
    if (entry_type == 0x00) break;

    if (entry_type == kExfatEntryTypeAllocationBitmap) {
      bitmap_cluster =
          *reinterpret_cast<const uint32_t*>(&root_dir_buf[offset + 20]);
      bitmap_length =
          *reinterpret_cast<const uint64_t*>(&root_dir_buf[offset + 24]);
    } else if (entry_type == kExfatEntryTypeVolumeLabel) {
      uint8_t char_count = root_dir_buf[offset + 1];
      const char16_t* chars =
          reinterpret_cast<const char16_t*>(&root_dir_buf[offset + 2]);
      std::string label;
      for (size_t i = 0; i < char_count && i < 11; i++) {
        if (chars[i] < 128) label.push_back(static_cast<char>(chars[i]));
      }
      if (!label.empty()) out_volume_name = label;
    }
  }

  if (bitmap_cluster >= 2 && bitmap_length > 0) {
    std::vector<uint8_t> bitmap_buf(bitmap_length);
    uint64_t bitmap_offset = cluster_to_offset(bitmap_cluster);
    if (reader(bitmap_offset, bitmap_length, bitmap_buf.data())) {
      uint64_t used_clusters = 0;
      for (uint32_t c = 0; c < cluster_count; c++) {
        size_t byte_idx = c / 8;
        size_t bit_pos = c % 8;
        if (byte_idx < bitmap_buf.size() &&
            (bitmap_buf[byte_idx] & (1 << bit_pos)))
          used_clusters++;
      }
      out_used_bytes = used_clusters * cluster_size;
      out_free_bytes = (cluster_count - used_clusters) * cluster_size;
      return true;
    }
  }

  out_used_bytes = cluster_size * 4;
  out_free_bytes = (cluster_count > 4) ? (cluster_count - 4) * cluster_size : 0;
  return true;
}

// Inspects ISO 9660 Primary Volume Descriptor.
bool AnalyzeIso9660Volume(const perception::disk::BlockReader& reader,
                          uint64_t& out_used_bytes, uint64_t& out_free_bytes,
                          std::string& out_volume_name) {
  uint64_t pvd_offset = kIso9660PvdSector * kOpticalSectorSize;
  std::vector<uint8_t> pvd(kOpticalSectorSize);
  if (!reader(pvd_offset, kOpticalSectorSize, pvd.data())) return false;

  if (pvd[0] == 1 && std::memcmp(&pvd[1], "CD001", 5) == 0) {
    uint32_t volume_space_size = *reinterpret_cast<const uint32_t*>(&pvd[80]);
    uint16_t logical_block_size = *reinterpret_cast<const uint16_t*>(&pvd[128]);

    out_used_bytes =
        static_cast<uint64_t>(volume_space_size) * logical_block_size;
    out_free_bytes = 0;

    std::string vol_id(reinterpret_cast<const char*>(&pvd[40]), 32);
    while (!vol_id.empty() && (vol_id.back() == ' ' || vol_id.back() == '\0'))
      vol_id.pop_back();

    if (!vol_id.empty()) out_volume_name = vol_id;
    return true;
  }
  return false;
}

}  // namespace

namespace perception {
namespace disk {

void AnalyzeFilesystem(uint64_t start_lba, uint64_t sector_count,
                       uint32_t sector_size, const BlockReader& reader,
                       PartitionInfo& partition) {
  uint64_t start_offset = start_lba * sector_size;
  std::vector<uint8_t> first_sector(sector_size);

  if (!reader(start_offset, sector_size, first_sector.data())) {
    partition.filesystem_type = FilesystemType::UNKNOWN;
    partition.filesystem_name =
        std::string(FilesystemTypeToString(FilesystemType::UNKNOWN));
    partition.used_bytes = 0;
    partition.free_bytes = partition.size_in_bytes;
    return;
  }

  if (CheckExfat(first_sector.data())) {
    partition.filesystem_type = FilesystemType::EXFAT;
    partition.filesystem_name =
        std::string(FilesystemTypeToString(FilesystemType::EXFAT));
    std::string vol_name;
    if (AnalyzeExfatVolume(start_offset, reader, partition.used_bytes,
                           partition.free_bytes, vol_name)) {
      if (!vol_name.empty()) partition.name = vol_name;
    }
    return;
  }

  partition.filesystem_type = FilesystemType::RAW;
  partition.filesystem_name = "Unformatted";
  partition.used_bytes = 0;
  partition.free_bytes = partition.size_in_bytes;
}

void AnalyzeRawDevice(uint64_t total_sectors, uint32_t sector_size,
                      const BlockReader& reader, DiskInfo& disk) {
  std::vector<uint8_t> first_sector(sector_size);
  if (!reader(0, sector_size, first_sector.data())) return;

  if (CheckExfat(first_sector.data())) {
    disk.raw_filesystem = FilesystemType::EXFAT;
    std::string vol_name;
    AnalyzeExfatVolume(0, reader, disk.raw_used_bytes, disk.raw_free_bytes,
                       vol_name);
    return;
  }

  if (AnalyzeIso9660Volume(reader, disk.raw_used_bytes, disk.raw_free_bytes,
                           disk.name)) {
    disk.raw_filesystem = FilesystemType::ISO9660;
    return;
  }

  disk.raw_filesystem = FilesystemType::RAW;
  disk.raw_used_bytes = 0;
  disk.raw_free_bytes = disk.size_in_bytes;
}

}  // namespace disk
}  // namespace perception
