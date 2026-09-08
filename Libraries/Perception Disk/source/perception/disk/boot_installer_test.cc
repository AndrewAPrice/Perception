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

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "perception/disk/disk_manager.h"
#include "perception/disk/disk_structures.h"
#include "testing.h"

namespace {

// Standard sector size in bytes.
constexpr uint32_t kSectorSize = 512;

using ::perception::disk::DiskInfo;
using ::perception::disk::DiskManager;
using ::perception::disk::EnsureGptBiosBootPartition;
using ::perception::disk::FreeSpaceRange;
using ::perception::disk::InstallGrubToGptDisk;
using ::perception::disk::InstallGrubToMbrDisk;
using ::perception::disk::PartitionInfo;
using ::perception::disk::PartitionScheme;

TEST(BootInstallerValidationSpansAndWritable) {
  DiskInfo disk;
  disk.is_writable = true;
  disk.sector_size = kSectorSize;
  disk.scheme = PartitionScheme::MBR;

  DiskManager disk_manager;

  std::vector<std::byte> empty_span;
  std::vector<std::byte> small_span(256, std::byte{0});
  std::vector<std::byte> valid_boot(512, std::byte{0});
  std::vector<std::byte> valid_core(1024, std::byte{0});

  // Empty spans rejected.
  EXPECT(false, InstallGrubToMbrDisk(disk, 2048, empty_span, valid_core,
                                     disk_manager));
  EXPECT(false, InstallGrubToMbrDisk(disk, 2048, valid_boot, empty_span,
                                     disk_manager));
  EXPECT(false, InstallGrubToGptDisk(disk, 2048, empty_span, valid_core,
                                     disk_manager));
  EXPECT(false, InstallGrubToGptDisk(disk, 2048, valid_boot, empty_span,
                                     disk_manager));

  // Small spans (< 512 bytes) rejected.
  EXPECT(false, InstallGrubToMbrDisk(disk, 2048, small_span, valid_core,
                                     disk_manager));
  EXPECT(false, InstallGrubToMbrDisk(disk, 2048, valid_boot, small_span,
                                     disk_manager));
  EXPECT(false, InstallGrubToGptDisk(disk, 2048, small_span, valid_core,
                                     disk_manager));
  EXPECT(false, InstallGrubToGptDisk(disk, 2048, valid_boot, small_span,
                                     disk_manager));

  // Read-only disk rejected.
  disk.is_writable = false;
  EXPECT(false, InstallGrubToMbrDisk(disk, 2048, valid_boot, valid_core,
                                     disk_manager));
  EXPECT(false, InstallGrubToGptDisk(disk, 2048, valid_boot, valid_core,
                                     disk_manager));

  // Non-512 sector size rejected.
  disk.is_writable = true;
  disk.sector_size = 2048;
  EXPECT(false, InstallGrubToMbrDisk(disk, 2048, valid_boot, valid_core,
                                     disk_manager));
  EXPECT(false, InstallGrubToGptDisk(disk, 2048, valid_boot, valid_core,
                                     disk_manager));
}

TEST(BootInstallerMbrGapAndGptLbaBoundaries) {
  DiskInfo disk;
  disk.is_writable = true;
  disk.sector_size = kSectorSize;
  disk.scheme = PartitionScheme::MBR;

  DiskManager disk_manager;

  std::vector<std::byte> boot_img(512, std::byte{0});
  // 3 sectors of core.img
  std::vector<std::byte> core_img(1536, std::byte{0});

  // core_sectors = 3, so core_sectors + 1 = 4.
  // Target partition starting at LBA 3 has no room for core image in MBR gap!
  EXPECT(false,
         InstallGrubToMbrDisk(disk, 3, boot_img, core_img, disk_manager));

  // GPT: bios_boot_start_lba < 34 collides with GPT header/partition array.
  disk.scheme = PartitionScheme::GPT;
  EXPECT(false,
         InstallGrubToGptDisk(disk, 33, boot_img, core_img, disk_manager));
  EXPECT(false, InstallGrubToGptDisk(disk, 0, boot_img, core_img, disk_manager));

  // With uninitialized mock device, ReadDeviceBytes returns false.
  EXPECT(false,
         InstallGrubToMbrDisk(disk, 2048, boot_img, core_img, disk_manager));
  EXPECT(false,
         InstallGrubToGptDisk(disk, 2048, boot_img, core_img, disk_manager));
}

TEST(EnsureGptBiosBootPartitionSchemeAndExisting) {
  DiskManager disk_manager;

  // Rejects non-GPT scheme.
  DiskInfo mbr_disk;
  mbr_disk.scheme = PartitionScheme::MBR;
  uint64_t out_lba = 0;
  EXPECT(false,
         EnsureGptBiosBootPartition(mbr_disk, out_lba, disk_manager));

  DiskInfo raw_disk;
  raw_disk.scheme = PartitionScheme::NONE;
  EXPECT(false,
         EnsureGptBiosBootPartition(raw_disk, out_lba, disk_manager));

  // Finds existing BIOS Boot Partition by type_name.
  DiskInfo gpt_disk_name;
  gpt_disk_name.scheme = PartitionScheme::GPT;
  PartitionInfo part1;
  part1.start_lba = 2048;
  part1.type_name = "BIOS Boot Partition";
  gpt_disk_name.partitions.push_back(part1);

  EXPECT(true,
         EnsureGptBiosBootPartition(gpt_disk_name, out_lba, disk_manager));
  EXPECT((uint64)2048, out_lba);

  // Finds existing BIOS Boot Partition by type_guid.
  DiskInfo gpt_disk_guid;
  gpt_disk_guid.scheme = PartitionScheme::GPT;
  PartitionInfo part2;
  part2.start_lba = 4096;
  part2.type_guid = "21686148-6449-6E6F-744E-656564454649";
  gpt_disk_guid.partitions.push_back(part2);

  EXPECT(true,
         EnsureGptBiosBootPartition(gpt_disk_guid, out_lba, disk_manager));
  EXPECT((uint64)4096, out_lba);
}

TEST(EnsureGptBiosBootPartitionInsufficientFreeSpace) {
  DiskManager disk_manager;

  // GPT disk with no BIOS boot partition and empty free space.
  DiskInfo gpt_empty;
  gpt_empty.scheme = PartitionScheme::GPT;
  uint64_t out_lba = 0;
  EXPECT(false,
         EnsureGptBiosBootPartition(gpt_empty, out_lba, disk_manager));

  // Free space ranges smaller than required 2048 sectors.
  DiskInfo gpt_small_free;
  gpt_small_free.scheme = PartitionScheme::GPT;
  FreeSpaceRange range;
  range.start_lba = 2048;
  range.sector_count = 1000;  // Less than 2048
  gpt_small_free.free_space_ranges.push_back(range);

  EXPECT(false,
         EnsureGptBiosBootPartition(gpt_small_free, out_lba, disk_manager));
}

}  // namespace
