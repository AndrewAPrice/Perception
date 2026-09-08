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

#include "perception/disk/boot_installer.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "perception/disk/gpt.h"
#include "perception/disk/mbr.h"

namespace {

// Standard PC BIOS sector size in bytes.
constexpr size_t kSectorSize = 512;

// Offset of core.img starting sector address in boot.img.
constexpr size_t kBootKernelSectorOffset = 0x5C;

// Offset of boot drive identifier in boot.img.
constexpr size_t kBootDriveOffset = 0x64;

// Default boot drive indicator (0xFF = use drive passed by BIOS in DL
// register).
constexpr uint8_t kBootDriveDefault = 0xFF;

// Maximum length of boot code to copy into sector 0 (preserving partition
// table).
constexpr size_t kBootCodeSize = 446;

// Offset of second sector address in diskboot.img (start of core.img).
constexpr size_t kDiskbootRemainingSectorsLbaOffset = 0x1F4;

// Offset of remaining sector count in diskboot.img.
constexpr size_t kDiskbootRemainingSectorsCountOffset = 0x1FC;

// Target memory segment for loading remaining core.img sectors.
constexpr uint16_t kDiskbootLoadSegment = 0x0820;

// Minimum required sectors for a BIOS Boot Partition (1 MiB = 2048 sectors of
// 512 bytes).
constexpr uint64_t kBiosBootPartitionSectors = 2048;

}  // namespace

namespace perception {
namespace disk {

bool InstallGrubToMbrDisk(DiskInfo& disk, uint64_t target_partition_start_lba,
                          std::span<const std::byte> boot_img,
                          std::span<const std::byte> core_img,
                          DiskManager& disk_manager) {
  if (!disk.is_writable || disk.sector_size != kSectorSize ||
      boot_img.size() < kSectorSize || core_img.size() < kSectorSize)
    return false;

  uint64_t core_sectors = (core_img.size() + kSectorSize - 1) / kSectorSize;
  if (target_partition_start_lba < core_sectors + 1) return false;

  std::vector<uint8_t> sector0(kSectorSize, 0);
  if (!disk_manager.ReadDeviceBytes(disk.device, 0, kSectorSize,
                                    sector0.data()))
    return false;

  // Copy boot code (first 446 bytes), preserving partition table at 446..510
  std::memcpy(sector0.data(), boot_img.data(), kBootCodeSize);

  // Patch starting sector of core.img (LBA 1 in post-MBR gap)
  uint64_t core_start_lba = 1;
  std::memcpy(&sector0[kBootKernelSectorOffset], &core_start_lba,
              sizeof(uint64_t));
  sector0[kBootDriveOffset] = kBootDriveDefault;
  sector0[510] = 0x55;
  sector0[511] = 0xAA;

  // Mark the target partition active in MBR partition table
  MbrPartitionEntry* entries =
      reinterpret_cast<MbrPartitionEntry*>(&sector0[446]);
  for (int i = 0; i < 4; i++) {
    if (entries[i].starting_lba ==
        static_cast<uint32_t>(target_partition_start_lba))
      entries[i].boot_indicator = 0x80;
  }

  // Patch core.img
  std::vector<uint8_t> patched_core(core_img.size());
  std::memcpy(patched_core.data(), core_img.data(), core_img.size());
  uint64_t remaining_start_lba = core_start_lba + 1;
  uint16_t remaining_count = static_cast<uint16_t>(core_sectors - 1);
  std::memcpy(&patched_core[kDiskbootRemainingSectorsLbaOffset],
              &remaining_start_lba, sizeof(uint64_t));
  std::memcpy(&patched_core[kDiskbootRemainingSectorsCountOffset],
              &remaining_count, sizeof(uint16_t));
  std::memcpy(&patched_core[kDiskbootRemainingSectorsCountOffset + 2],
              &kDiskbootLoadSegment, sizeof(uint16_t));

  // Write sector 0
  if (!disk_manager.WriteDeviceBytes(disk.device, 0, kSectorSize,
                                     sector0.data()))
    return false;

  // Write core.img starting at sector 1
  if (!disk_manager.WriteDeviceBytes(disk.device, core_start_lba * kSectorSize,
                                     patched_core.size(), patched_core.data()))
    return false;

  return true;
}

bool InstallGrubToGptDisk(DiskInfo& disk, uint64_t bios_boot_start_lba,
                          std::span<const std::byte> boot_img,
                          std::span<const std::byte> core_img,
                          DiskManager& disk_manager) {
  if (!disk.is_writable || disk.sector_size != kSectorSize ||
      boot_img.size() < kSectorSize || core_img.size() < kSectorSize ||
      bios_boot_start_lba < 34)
    return false;

  uint64_t core_sectors = (core_img.size() + kSectorSize - 1) / kSectorSize;

  std::vector<uint8_t> sector0(kSectorSize, 0);
  if (!disk_manager.ReadDeviceBytes(disk.device, 0, kSectorSize,
                                    sector0.data()))
    return false;

  // Copy boot code (first 446 bytes), preserving protective MBR entry at
  // 446..510
  std::memcpy(sector0.data(), boot_img.data(), kBootCodeSize);

  // Patch starting sector of core.img to point to BIOS Boot Partition
  std::memcpy(&sector0[kBootKernelSectorOffset], &bios_boot_start_lba,
              sizeof(uint64_t));
  sector0[kBootDriveOffset] = kBootDriveDefault;
  sector0[510] = 0x55;
  sector0[511] = 0xAA;

  // Patch core.img
  std::vector<uint8_t> patched_core(core_img.size());
  std::memcpy(patched_core.data(), core_img.data(), core_img.size());
  uint64_t remaining_start_lba = bios_boot_start_lba + 1;
  uint16_t remaining_count = static_cast<uint16_t>(core_sectors - 1);
  std::memcpy(&patched_core[kDiskbootRemainingSectorsLbaOffset],
              &remaining_start_lba, sizeof(uint64_t));
  std::memcpy(&patched_core[kDiskbootRemainingSectorsCountOffset],
              &remaining_count, sizeof(uint16_t));
  std::memcpy(&patched_core[kDiskbootRemainingSectorsCountOffset + 2],
              &kDiskbootLoadSegment, sizeof(uint16_t));

  // Write sector 0
  if (!disk_manager.WriteDeviceBytes(disk.device, 0, kSectorSize,
                                     sector0.data()))
    return false;

  // Write core.img into BIOS Boot Partition
  if (!disk_manager.WriteDeviceBytes(disk.device,
                                     bios_boot_start_lba * kSectorSize,
                                     patched_core.size(), patched_core.data()))
    return false;

  return true;
}

bool EnsureGptBiosBootPartition(DiskInfo& disk,
                                uint64_t& out_bios_boot_start_lba,
                                DiskManager& disk_manager) {
  if (disk.scheme != PartitionScheme::GPT) return false;

  for (const auto& part : disk.partitions) {
    if (IsGptBiosBootPartition(part)) {
      out_bios_boot_start_lba = part.start_lba;
      return true;
    }
  }

  for (const auto& range : disk.free_space_ranges) {
    if (range.sector_count >= kBiosBootPartitionSectors) {
      if (disk_manager.AddPartition(disk, range.start_lba,
                                    kBiosBootPartitionSectors, "BIOS Boot")) {
        out_bios_boot_start_lba = range.start_lba;
        return true;
      }
    }
  }

  return false;
}

}  // namespace disk
}  // namespace perception
