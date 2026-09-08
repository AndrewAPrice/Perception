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

#include <cstdint>
#include <cstring>
#include <vector>

#include "perception/disk/disk_structures.h"
#include "testing.h"

namespace {

// Standard sector size in bytes.
constexpr uint32_t kSectorSize = 512;

// Total sector count for mock 1GB disk.
constexpr uint64_t kTestDiskSectors = 2097152;

// Standard 1 MiB alignment offset in 512-byte sectors.
constexpr uint64_t kAlignmentSectors = 2048;

using ::perception::disk::AddMbrPartitionToSector;
using ::perception::disk::CreateEmptyMbrSector;
using ::perception::disk::DeleteMbrPartitionFromSector;
using ::perception::disk::DetectMbr;
using ::perception::disk::FreeSpaceRange;
using ::perception::disk::GetMbrTypeName;
using ::perception::disk::ParseMbrPartitions;
using ::perception::disk::PartitionInfo;

TEST(MbrCreateEmptySector) {
  std::vector<uint8_t> sector = CreateEmptyMbrSector();
  EXPECT((size_t)512, sector.size());

  for (size_t i = 0; i < 510; i++) EXPECT((uint8_t)0, sector[i]);

  EXPECT((uint8_t)0x55, sector[510]);
  EXPECT((uint8_t)0xAA, sector[511]);
}

TEST(MbrDetectValidAndEmpty) {
  std::vector<uint8_t> sector = CreateEmptyMbrSector();
  EXPECT(true, DetectMbr(sector.data(), kTestDiskSectors));

  EXPECT(true, AddMbrPartitionToSector(sector.data(), 2048, 100000, 0x07));
  EXPECT(true, DetectMbr(sector.data(), kTestDiskSectors));
}

TEST(MbrDetectCorruptedSignatures) {
  std::vector<uint8_t> sector = CreateEmptyMbrSector();

  sector[510] = 0x00;
  EXPECT(false, DetectMbr(sector.data(), kTestDiskSectors));

  sector[510] = 0x55;
  sector[511] = 0x00;
  EXPECT(false, DetectMbr(sector.data(), kTestDiskSectors));

  sector[510] = 0xAA;
  sector[511] = 0x55;
  EXPECT(false, DetectMbr(sector.data(), kTestDiskSectors));
}

TEST(MbrDetectRejectsExfatAndGptProtective) {
  std::vector<uint8_t> exfat_sector(kSectorSize, 0);
  std::memcpy(&exfat_sector[3], "EXFAT   ", 8);
  exfat_sector[510] = 0x55;
  exfat_sector[511] = 0xAA;
  EXPECT(false, DetectMbr(exfat_sector.data(), kTestDiskSectors));

  std::vector<uint8_t> gpt_pmbr = CreateEmptyMbrSector();
  gpt_pmbr[446 + 4] = 0xEE;
  EXPECT(false, DetectMbr(gpt_pmbr.data(), kTestDiskSectors));
}

TEST(MbrDetectBootstrapCodeAndBounds) {
  std::vector<uint8_t> sector = CreateEmptyMbrSector();
  sector[10] = 0x90;
  EXPECT(false, DetectMbr(sector.data(), kTestDiskSectors));

  EXPECT(true, AddMbrPartitionToSector(sector.data(), 2048, 100000, 0x83));
  EXPECT(true, DetectMbr(sector.data(), kTestDiskSectors));

  std::vector<uint8_t> out_of_bounds = CreateEmptyMbrSector();
  out_of_bounds[10] = 0x90;
  EXPECT(true, AddMbrPartitionToSector(out_of_bounds.data(),
                                       kTestDiskSectors + 5000, 1000, 0x83));
  EXPECT(false, DetectMbr(out_of_bounds.data(), kTestDiskSectors));
}

TEST(MbrAddAndFullTable) {
  std::vector<uint8_t> sector = CreateEmptyMbrSector();

  EXPECT(true, AddMbrPartitionToSector(sector.data(), 2048, 10000, 0x07));
  EXPECT(true, AddMbrPartitionToSector(sector.data(), 12048, 10000, 0x0B));
  EXPECT(true, AddMbrPartitionToSector(sector.data(), 22048, 10000, 0x0C));
  EXPECT(true, AddMbrPartitionToSector(sector.data(), 32048, 10000, 0x83));

  // Fifth partition must fail as MBR only supports 4 primary partitions.
  EXPECT(false, AddMbrPartitionToSector(sector.data(), 42048, 10000, 0x83));
}

TEST(MbrDeletePartitionAndReuse) {
  std::vector<uint8_t> sector = CreateEmptyMbrSector();

  EXPECT(true, AddMbrPartitionToSector(sector.data(), 2048, 10000, 0x07));
  EXPECT(true, AddMbrPartitionToSector(sector.data(), 12048, 10000, 0x83));

  EXPECT(false, DeleteMbrPartitionFromSector(sector.data(), -1));
  EXPECT(false, DeleteMbrPartitionFromSector(sector.data(), 4));
  EXPECT(false, DeleteMbrPartitionFromSector(sector.data(), 10));

  // Delete partition 0.
  EXPECT(true, DeleteMbrPartitionFromSector(sector.data(), 0));

  // Adding new partition reuses slot 0.
  EXPECT(true, AddMbrPartitionToSector(sector.data(), 4096, 5000, 0xEF));

  std::vector<PartitionInfo> partitions;
  std::vector<FreeSpaceRange> free_ranges;
  ParseMbrPartitions(sector.data(), kTestDiskSectors, kSectorSize, partitions,
                     free_ranges);
  ASSERT((size_t)2, partitions.size());
  EXPECT((int)1, partitions[0].partition_number);
  EXPECT((uint64)4096, partitions[0].start_lba);
  EXPECT((uint8)0xEF, partitions[0].mbr_type);
}

TEST(MbrParseFreeSpaceFiltering) {
  std::vector<uint8_t> sector = CreateEmptyMbrSector();

  // Empty MBR free space.
  std::vector<PartitionInfo> partitions;
  std::vector<FreeSpaceRange> free_ranges;
  ParseMbrPartitions(sector.data(), kTestDiskSectors, kSectorSize, partitions,
                     free_ranges);
  EXPECT((size_t)0, partitions.size());
  ASSERT((size_t)1, free_ranges.size());
  EXPECT(kAlignmentSectors, free_ranges[0].start_lba);
  EXPECT(kTestDiskSectors - 1, free_ranges[0].end_lba);
  EXPECT(kTestDiskSectors - kAlignmentSectors, free_ranges[0].sector_count);

  // Partition at LBA 4096 leaves a 2048-sector gap before it.
  EXPECT(true, AddMbrPartitionToSector(sector.data(), 4096, 10000, 0x07));
  // Partition at LBA 14196 leaves a 100-sector gap (< 2048 alignment).
  EXPECT(true, AddMbrPartitionToSector(sector.data(), 14196, 5000, 0x83));

  partitions.clear();
  free_ranges.clear();
  ParseMbrPartitions(sector.data(), kTestDiskSectors, kSectorSize, partitions,
                     free_ranges);
  ASSERT((size_t)2, partitions.size());
  // Gap before partition 1 (2048..4095 is 2048 sectors) and trailing gap.
  // The 100-sector gap (14096..14195) should be filtered out.
  ASSERT((size_t)2, free_ranges.size());
  EXPECT(kAlignmentSectors, free_ranges[0].start_lba);
  EXPECT((uint64)4095, free_ranges[0].end_lba);
  EXPECT((uint64)2048, free_ranges[0].sector_count);

  EXPECT((uint64)19196, free_ranges[1].start_lba);
  EXPECT(kTestDiskSectors - 1, free_ranges[1].end_lba);
}

TEST(MbrParseOutOfOrderPartitions) {
  std::vector<uint8_t> sector = CreateEmptyMbrSector();

  // Slot 0 has higher LBA than slot 1.
  EXPECT(true, AddMbrPartitionToSector(sector.data(), 50000, 10000, 0x07));
  EXPECT(true, AddMbrPartitionToSector(sector.data(), 2048, 5000, 0x83));

  std::vector<PartitionInfo> partitions;
  std::vector<FreeSpaceRange> free_ranges;
  ParseMbrPartitions(sector.data(), kTestDiskSectors, kSectorSize, partitions,
                     free_ranges);
  ASSERT((size_t)2, partitions.size());
  EXPECT((int)1, partitions[0].partition_number);
  EXPECT((uint64)50000, partitions[0].start_lba);
  EXPECT((int)2, partitions[1].partition_number);
  EXPECT((uint64)2048, partitions[1].start_lba);

  // Extents sorted internally: gap between 7048 and 49999 is recorded.
  ASSERT((size_t)2, free_ranges.size());
  EXPECT((uint64)7048, free_ranges[0].start_lba);
  EXPECT((uint64)49999, free_ranges[0].end_lba);
}

TEST(MbrTypeNameLookups) {
  EXPECT(std::string("exFAT / NTFS"), GetMbrTypeName(0x07));
  EXPECT(std::string("FAT32 CHS"), GetMbrTypeName(0x0B));
  EXPECT(std::string("FAT32 LBA"), GetMbrTypeName(0x0C));
  EXPECT(std::string("Linux Native"), GetMbrTypeName(0x83));
  EXPECT(std::string("GPT Protective"), GetMbrTypeName(0xEE));
  EXPECT(std::string("EFI System"), GetMbrTypeName(0xEF));
  EXPECT(std::string("Type 0x00"), GetMbrTypeName(0x00));
  EXPECT(std::string("Type 0x82"), GetMbrTypeName(0x82));
  EXPECT(std::string("Type 0xFD"), GetMbrTypeName(0xFD));
}

}  // namespace
