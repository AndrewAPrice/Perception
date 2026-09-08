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

#include "perception/disk/filesystems/exfat.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "perception/random.h"

namespace {

// Sector size in bytes.
constexpr size_t kSectorSize = 512;

// Sectors per cluster shift (8 sectors = 4096 bytes).
constexpr uint8_t kSectorsPerClusterShift = 3;

// Bytes per sector shift (512 bytes = 2^9).
constexpr uint8_t kBytesPerSectorShift = 9;

// Total sectors per cluster.
constexpr uint32_t kSectorsPerCluster = 1 << kSectorsPerClusterShift;

// Cluster size in bytes.
constexpr size_t kClusterSize = kSectorSize * kSectorsPerCluster;

// Number of sectors in Main Boot Region.
constexpr uint32_t kBootRegionSectors = 12;

// Standard exFAT FAT end-of-chain value.
constexpr uint32_t kFatEndOfChain = 0xFFFFFFFF;

// Standard exFAT FAT media type value for fixed disk.
constexpr uint32_t kFatMediaType = 0xFFFFFFF8;

// Computes 32-bit exFAT boot checksum across the 11 boot sectors.
uint32_t ComputeBootChecksum(const uint8_t* boot_region, size_t num_sectors) {
  uint32_t checksum = 0;
  size_t total_bytes = num_sectors * kSectorSize;
  for (size_t i = 0; i < total_bytes; i++) {
    // Skip VolumeFlags (bytes 106, 107) and PercentInUse (byte 112) of sector 0
    if (i == 106 || i == 107 || i == 112) continue;
    checksum = ((checksum << 31) | (checksum >> 1)) + boot_region[i];
  }
  return checksum;
}

}  // namespace

namespace perception {
namespace disk {
namespace filesystems {

bool FormatExfat(uint64_t start_lba, uint64_t sector_count,
                 uint32_t sector_size, const std::string& volume_label,
                 const BlockWriter& writer) {
  if (sector_count < 2048) return false;

  uint64_t base_offset = start_lba * sector_size;

  // FAT offset = 24 sectors (Main boot region + Backup boot region)
  uint32_t fat_offset = 24;

  // Align FAT offset to cluster boundary (8 sectors)
  fat_offset =
      (fat_offset + kSectorsPerCluster - 1) & ~(kSectorsPerCluster - 1);

  // Estimate cluster heap offset and cluster count
  uint32_t estimated_cluster_count =
      static_cast<uint32_t>((sector_count - fat_offset) / kSectorsPerCluster);
  uint32_t fat_size_bytes = (estimated_cluster_count + 2) * 4;
  uint32_t fat_length =
      static_cast<uint32_t>((fat_size_bytes + kSectorSize - 1) / kSectorSize);
  fat_length =
      (fat_length + kSectorsPerCluster - 1) & ~(kSectorsPerCluster - 1);

  uint32_t cluster_heap_offset = fat_offset + fat_length;
  if (cluster_heap_offset >= sector_count) return false;

  uint32_t cluster_count = static_cast<uint32_t>(
      (sector_count - cluster_heap_offset) / kSectorsPerCluster);
  if (cluster_count < 16) return false;

  uint32_t bitmap_first_cluster = 2;
  uint64_t bitmap_size_bytes = (cluster_count + 7) / 8;
  uint32_t upcase_first_cluster = 3;
  uint32_t root_dir_first_cluster = 4;

  uint32_t volume_serial = static_cast<uint32_t>(perception::RandomNumber());

  // Construct Main Boot Region (12 sectors = 6144 bytes)
  std::vector<uint8_t> main_boot_region(kBootRegionSectors * kSectorSize, 0);

  // Sector 0: Boot Sector
  uint8_t* bs = &main_boot_region[0];
  bs[0] = 0xEB;  // Jump boot instruction
  bs[1] = 0x76;
  bs[2] = 0x90;
  std::memcpy(&bs[3], "EXFAT   ", 8);  // OEM Name

  uint64_t partition_offset = start_lba;
  uint64_t volume_length = sector_count;
  std::memcpy(&bs[64], &partition_offset, 8);
  std::memcpy(&bs[72], &volume_length, 8);
  std::memcpy(&bs[80], &fat_offset, 4);
  std::memcpy(&bs[84], &fat_length, 4);
  std::memcpy(&bs[88], &cluster_heap_offset, 4);
  std::memcpy(&bs[92], &cluster_count, 4);
  std::memcpy(&bs[96], &root_dir_first_cluster, 4);
  std::memcpy(&bs[100], &volume_serial, 4);

  bs[104] = 0x00;  // FileSystemRevision 1.00
  bs[105] = 0x01;
  bs[106] = 0x00;  // VolumeFlags (active FAT 0)
  bs[107] = 0x00;
  bs[108] = kBytesPerSectorShift;     // 9 (512 bytes)
  bs[109] = kSectorsPerClusterShift;  // 3 (8 sectors = 4096 bytes)
  bs[110] = 1;                        // NumberOfFATs = 1
  bs[111] = 0x80;                     // DriveSelect
  bs[112] = 0;                        // PercentInUse

  bs[510] = 0x55;
  bs[511] = 0xAA;

  // Sectors 1..8: Extended Boot Sectors
  for (int s = 1; s <= 8; s++) {
    main_boot_region[s * kSectorSize + 510] = 0x55;
    main_boot_region[s * kSectorSize + 511] = 0xAA;
  }

  // Sector 9: OEM Parameter Sector
  main_boot_region[9 * kSectorSize + 510] = 0x55;
  main_boot_region[9 * kSectorSize + 511] = 0xAA;

  // Compute checksum across sectors 0..10
  uint32_t checksum = ComputeBootChecksum(main_boot_region.data(), 11);

  // Sector 11: Checksum Sector (repeats 32-bit checksum 128 times)
  for (int i = 0; i < 128; i++)
    std::memcpy(&main_boot_region[11 * kSectorSize + i * 4], &checksum, 4);

  // Write Main Boot Region
  if (!writer(base_offset, main_boot_region.size(), main_boot_region.data()))
    return false;

  // Write Backup Boot Region (Sector 12..23)
  if (!writer(base_offset + 12 * kSectorSize, main_boot_region.size(),
              main_boot_region.data()))
    return false;

  // Write FAT Table
  std::vector<uint32_t> fat_table(fat_length * (kSectorSize / 4), 0);
  fat_table[0] = kFatMediaType;
  fat_table[1] = kFatEndOfChain;
  fat_table[bitmap_first_cluster] = kFatEndOfChain;
  fat_table[upcase_first_cluster] = kFatEndOfChain;
  fat_table[root_dir_first_cluster] = kFatEndOfChain;

  uint64_t fat_device_offset = base_offset + fat_offset * kSectorSize;
  if (!writer(fat_device_offset, fat_table.size() * sizeof(uint32_t),
              fat_table.data()))
    return false;

  // Write Allocation Bitmap (Cluster 2)
  std::vector<uint8_t> bitmap(kClusterSize, 0);
  // Mark cluster 2 (bitmap), cluster 3 (upcase), cluster 4 (root dir) as
  // allocated
  bitmap[0] = 0x07;  // bits 0, 1, 2 = 1
  uint64_t bitmap_offset =
      base_offset +
      (cluster_heap_offset + (bitmap_first_cluster - 2) * kSectorsPerCluster) *
          kSectorSize;
  if (!writer(bitmap_offset, bitmap.size(), bitmap.data())) return false;

  // Write Up-Case Table (Cluster 3)
  std::vector<uint8_t> upcase(kClusterSize, 0);
  // Identity ASCII upcase table (0..127)
  for (int i = 0; i < 128; i++) {
    char16_t ch = static_cast<char16_t>(i);
    if (ch >= u'a' && ch <= u'z') ch = ch - u'a' + u'A';
    std::memcpy(&upcase[i * 2], &ch, 2);
  }
  uint64_t upcase_offset =
      base_offset +
      (cluster_heap_offset + (upcase_first_cluster - 2) * kSectorsPerCluster) *
          kSectorSize;
  if (!writer(upcase_offset, upcase.size(), upcase.data())) return false;

  // Write Root Directory (Cluster 4)
  std::vector<uint8_t> root_dir(kClusterSize, 0);

  // Entry 0: Allocation Bitmap Entry (32 bytes)
  root_dir[0] = 0x81;  // EntryType Allocation Bitmap
  root_dir[1] = 0x00;  // BitmapFlags (1st bitmap)
  std::memcpy(&root_dir[20], &bitmap_first_cluster, 4);
  std::memcpy(&root_dir[24], &bitmap_size_bytes, 8);

  // Entry 1: Up-Case Table Entry (32 bytes)
  root_dir[32] = 0x82;  // EntryType Up-case Table
  uint64_t upcase_length = 5836;
  std::memcpy(&root_dir[32 + 20], &upcase_first_cluster, 4);
  std::memcpy(&root_dir[32 + 24], &upcase_length, 8);

  // Entry 2: Volume Label Entry (32 bytes)
  std::string label = volume_label.empty() ? "PERCEPTION" : volume_label;
  if (label.size() > 11) label = label.substr(0, 11);

  root_dir[64] = 0x83;  // EntryType Volume Label
  root_dir[65] = static_cast<uint8_t>(label.size());
  for (size_t i = 0; i < label.size(); i++) {
    char16_t ch = static_cast<char16_t>(label[i]);
    std::memcpy(&root_dir[64 + 2 + i * 2], &ch, 2);
  }

  uint64_t root_dir_offset =
      base_offset + (cluster_heap_offset +
                     (root_dir_first_cluster - 2) * kSectorsPerCluster) *
                        kSectorSize;
  if (!writer(root_dir_offset, root_dir.size(), root_dir.data())) return false;

  return true;
}

bool UpdateExfatPartitionOffset(uint64_t new_start_lba, uint32_t sector_size,
                                const BlockReader& reader,
                                const BlockWriter& writer) {
  if (sector_size != kSectorSize) return false;

  uint64_t base_offset = new_start_lba * sector_size;
  std::vector<uint8_t> main_boot_region(kBootRegionSectors * kSectorSize, 0);

  if (!reader(base_offset, main_boot_region.size(), main_boot_region.data()))
    return false;

  uint8_t* bs = &main_boot_region[0];
  if (bs[0] != 0xEB || bs[1] != 0x76 || bs[2] != 0x90) return false;
  if (std::memcmp(&bs[3], "EXFAT   ", 8) != 0) return false;
  if (bs[510] != 0x55 || bs[511] != 0xAA) return false;

  uint64_t partition_offset = new_start_lba;
  std::memcpy(&bs[64], &partition_offset, 8);

  uint32_t checksum = ComputeBootChecksum(main_boot_region.data(), 11);

  for (int i = 0; i < 128; i++)
    std::memcpy(&main_boot_region[11 * kSectorSize + i * 4], &checksum, 4);

  if (!writer(base_offset, main_boot_region.size(), main_boot_region.data()))
    return false;

  if (!writer(base_offset + 12 * kSectorSize, main_boot_region.size(),
              main_boot_region.data()))
    return false;

  return true;
}

}  // namespace filesystems
}  // namespace disk
}  // namespace perception
