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

#include "perception/disk/gpt.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "perception/disk/crc32.h"
#include "perception/disk/disk_structures.h"
#include "perception/disk/mbr.h"
#include "testing.h"

namespace {

// Standard sector size in bytes.
constexpr uint32_t kSectorSize = 512;

// Total sector count for mock 1GB disk.
constexpr uint64_t kTestDiskSectors = 2097152;

// Standard GPT signature "EFI PART" in little-endian uint64.
constexpr uint64_t kGptSignature = 0x5452415020494645ULL;

// Linux Filesystem Data GUID: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
constexpr uint8_t kLinuxDataGuid[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84,
                                        0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69,
                                        0xD8, 0x47, 0x7D, 0xE4};

using ::perception::disk::AddGptPartitionToEntries;
using ::perception::disk::AddMbrPartitionToSector;
using ::perception::disk::CalculateCrc32;
using ::perception::disk::CreateEmptyGptStructures;
using ::perception::disk::CreateEmptyMbrSector;
using ::perception::disk::CreateProtectiveMbrSector;
using ::perception::disk::DeleteGptPartitionFromEntries;
using ::perception::disk::DetectGpt;
using ::perception::disk::DetectMbr;
using ::perception::disk::FormatGuid;
using ::perception::disk::FreeSpaceRange;
using ::perception::disk::GetBasicDataGuid;
using ::perception::disk::GetBiosBootGuid;
using ::perception::disk::GetGptTypeName;
using ::perception::disk::GptHeader;
using ::perception::disk::GptPartitionEntry;
using ::perception::disk::IsGptBiosBootPartition;
using ::perception::disk::ParseGptPartitions;
using ::perception::disk::PartitionInfo;

TEST(GptDetectScenarios) {
  std::vector<uint8_t> pmbr = CreateProtectiveMbrSector(kTestDiskSectors);
  std::vector<uint8_t> p_hdr, p_ent, b_hdr, b_ent;
  CreateEmptyGptStructures(kTestDiskSectors, kSectorSize, p_hdr, p_ent, b_hdr,
                           b_ent);

  // Valid PMBR and valid primary GPT header.
  EXPECT(true, DetectGpt(pmbr.data(), p_hdr.data()));

  // sector0 == nullptr should check sector1 alone.
  EXPECT(true, DetectGpt(nullptr, p_hdr.data()));

  // sector1 == nullptr should return false.
  EXPECT(false, DetectGpt(pmbr.data(), nullptr));

  // Invalid signature in sector 0.
  pmbr[510] = 0x00;
  EXPECT(false, DetectGpt(pmbr.data(), p_hdr.data()));
  pmbr[510] = 0x55;

  // Invalid PMBR partition type (not 0xEE).
  pmbr[446 + 4] = 0x83;
  EXPECT(false, DetectGpt(pmbr.data(), p_hdr.data()));
  pmbr[446 + 4] = 0xEE;

  // Invalid signature in sector 1 GPT header.
  std::vector<uint8_t> invalid_hdr = p_hdr;
  *reinterpret_cast<uint64_t*>(invalid_hdr.data()) = 0x12345678ULL;
  EXPECT(false, DetectGpt(pmbr.data(), invalid_hdr.data()));
}

TEST(GptProtectiveMbrFields) {
  std::vector<uint8_t> pmbr = CreateProtectiveMbrSector(kTestDiskSectors);
  EXPECT((size_t)512, pmbr.size());
  EXPECT((uint8_t)0x55, pmbr[510]);
  EXPECT((uint8_t)0xAA, pmbr[511]);

  EXPECT((uint8_t)0x00, pmbr[446]);      // Non-bootable
  EXPECT((uint8_t)0xEE, pmbr[446 + 4]);  // Protective MBR type
  uint32_t start_lba = *reinterpret_cast<uint32_t*>(&pmbr[454]);
  EXPECT((uint32_t)1, start_lba);

  uint32_t sector_count = *reinterpret_cast<uint32_t*>(&pmbr[458]);
  EXPECT((uint32_t)(kTestDiskSectors - 1), sector_count);

  // Test 32-bit clamp for large disk (> 4 billion sectors).
  uint64_t large_sectors = 0x200000000ULL;
  std::vector<uint8_t> large_pmbr = CreateProtectiveMbrSector(large_sectors);
  uint32_t clamped_count = *reinterpret_cast<uint32_t*>(&large_pmbr[458]);
  EXPECT((uint32_t)0xFFFFFFFF, clamped_count);
}

TEST(GptCreateEmptyStructuresAndCrcVerification) {
  std::vector<uint8_t> p_hdr, p_ent, b_hdr, b_ent;
  CreateEmptyGptStructures(kTestDiskSectors, kSectorSize, p_hdr, p_ent, b_hdr,
                           b_ent);

  const GptHeader* primary = reinterpret_cast<const GptHeader*>(p_hdr.data());
  EXPECT(kGptSignature, primary->signature);
  EXPECT((uint32_t)0x00010000, primary->revision);
  EXPECT((uint32_t)92, primary->header_size);
  EXPECT((uint64_t)1, primary->current_lba);
  EXPECT(kTestDiskSectors - 1, primary->backup_lba);
  EXPECT((uint32_t)128, primary->num_partition_entries);
  EXPECT((uint32_t)128, primary->sizeof_partition_entry);

  // Verify UUID v4 bits on disk GUID.
  EXPECT((uint8_t)0x40, (uint8_t)(primary->disk_guid[6] & 0xF0));
  EXPECT((uint8_t)0x80, (uint8_t)(primary->disk_guid[8] & 0xC0));

  // Verify primary entries CRC32.
  uint32_t computed_entries_crc = CalculateCrc32(p_ent.data(), p_ent.size());
  EXPECT(computed_entries_crc, primary->partition_entries_crc32);

  // Verify primary header CRC32.
  GptHeader primary_copy = *primary;
  primary_copy.header_crc32 = 0;
  uint32_t computed_header_crc = CalculateCrc32(
      reinterpret_cast<const uint8_t*>(&primary_copy), primary_copy.header_size);
  EXPECT(computed_header_crc, primary->header_crc32);

  // Verify backup header.
  const GptHeader* backup = reinterpret_cast<const GptHeader*>(b_hdr.data());
  EXPECT(kTestDiskSectors - 1, backup->current_lba);
  EXPECT((uint64_t)1, backup->backup_lba);
  EXPECT(computed_entries_crc, backup->partition_entries_crc32);

  GptHeader backup_copy = *backup;
  backup_copy.header_crc32 = 0;
  uint32_t computed_backup_crc = CalculateCrc32(
      reinterpret_cast<const uint8_t*>(&backup_copy), backup_copy.header_size);
  EXPECT(computed_backup_crc, backup->header_crc32);
}

TEST(GptAddPartitionsAndTypeVerification) {
  std::vector<uint8_t> p_hdr, p_ent, b_hdr, b_ent;
  CreateEmptyGptStructures(kTestDiskSectors, kSectorSize, p_hdr, p_ent, b_hdr,
                           b_ent);

  GptHeader* p_header = reinterpret_cast<GptHeader*>(p_hdr.data());
  GptHeader* b_header = reinterpret_cast<GptHeader*>(b_hdr.data());

  // Add BIOS Boot Partition.
  EXPECT(true,
         AddGptPartitionToEntries(p_ent, *p_header, *b_header, 2048, 2048,
                                  "BIOS Boot Partition", GetBiosBootGuid()));

  // Add Basic Data Partition (default nullptr type_guid).
  EXPECT(true, AddGptPartitionToEntries(p_ent, *p_header, *b_header, 4096,
                                       100000, "Perception OS", nullptr));

  // Add Linux Partition.
  EXPECT(true, AddGptPartitionToEntries(p_ent, *p_header, *b_header, 104096,
                                       50000, "Linux Root", kLinuxDataGuid));

  std::vector<PartitionInfo> partitions;
  std::vector<FreeSpaceRange> free_ranges;
  EXPECT(true, ParseGptPartitions(p_hdr.data(), p_ent.data(), kTestDiskSectors,
                                 kSectorSize, partitions, free_ranges));

  ASSERT((size_t)3, partitions.size());
  EXPECT(true, IsGptBiosBootPartition(partitions[0]));
  EXPECT(false, IsGptBiosBootPartition(partitions[1]));
  EXPECT(false, IsGptBiosBootPartition(partitions[2]));

  EXPECT(std::string("BIOS Boot Partition"), partitions[0].type_name);
  EXPECT(std::string("Basic Data (exFAT)"), partitions[1].type_name);
  EXPECT(std::string("Linux Filesystem"), partitions[2].type_name);

  EXPECT(std::string("BIOS Boot Partition"), partitions[0].name);
  EXPECT(std::string("Perception OS"), partitions[1].name);
  EXPECT(std::string("Linux Root"), partitions[2].name);
}

TEST(GptUtf8MultibyteNameHandling) {
  std::vector<uint8_t> p_hdr, p_ent, b_hdr, b_ent;
  CreateEmptyGptStructures(kTestDiskSectors, kSectorSize, p_hdr, p_ent, b_hdr,
                           b_ent);

  GptHeader* p_header = reinterpret_cast<GptHeader*>(p_hdr.data());
  GptHeader* b_header = reinterpret_cast<GptHeader*>(b_hdr.data());

  // UTF-8 with multibyte character.
  std::string utf8_name = "Percepti\xC3\xB3n";
  EXPECT(true, AddGptPartitionToEntries(p_ent, *p_header, *b_header, 2048, 2048,
                                       utf8_name, GetBasicDataGuid()));

  std::vector<PartitionInfo> partitions;
  std::vector<FreeSpaceRange> free_ranges;
  EXPECT(true, ParseGptPartitions(p_hdr.data(), p_ent.data(), kTestDiskSectors,
                                 kSectorSize, partitions, free_ranges));
  ASSERT((size_t)1, partitions.size());
  EXPECT(utf8_name, partitions[0].name);
}

TEST(GptDeletePartitionAndTableFull) {
  std::vector<uint8_t> p_hdr, p_ent, b_hdr, b_ent;
  CreateEmptyGptStructures(kTestDiskSectors, kSectorSize, p_hdr, p_ent, b_hdr,
                           b_ent);

  GptHeader* p_header = reinterpret_cast<GptHeader*>(p_hdr.data());
  GptHeader* b_header = reinterpret_cast<GptHeader*>(b_hdr.data());

  EXPECT(false,
         DeleteGptPartitionFromEntries(p_ent, *p_header, *b_header, -1));
  EXPECT(false,
         DeleteGptPartitionFromEntries(p_ent, *p_header, *b_header, 128));

  // Fill all 128 entries.
  for (uint32_t i = 0; i < 128; i++) {
    uint64_t start = 2048 + i * 10;
    EXPECT(true, AddGptPartitionToEntries(p_ent, *p_header, *b_header, start,
                                         10, "Part", GetBasicDataGuid()));
  }

  // 129th must fail.
  EXPECT(false, AddGptPartitionToEntries(p_ent, *p_header, *b_header, 10000,
                                        10, "Overflow", GetBasicDataGuid()));

  // Delete partition 50.
  EXPECT(true,
         DeleteGptPartitionFromEntries(p_ent, *p_header, *b_header, 50));

  // Now an add will succeed into the vacated slot.
  EXPECT(true, AddGptPartitionToEntries(p_ent, *p_header, *b_header, 10000, 10,
                                       "Replaced", GetBasicDataGuid()));
}

TEST(GptParseFilteringInvalidEntries) {
  std::vector<uint8_t> p_hdr, p_ent, b_hdr, b_ent;
  CreateEmptyGptStructures(kTestDiskSectors, kSectorSize, p_hdr, p_ent, b_hdr,
                           b_ent);

  GptHeader* p_header = reinterpret_cast<GptHeader*>(p_hdr.data());
  GptHeader* b_header = reinterpret_cast<GptHeader*>(b_hdr.data());

  // Add valid partition.
  EXPECT(true, AddGptPartitionToEntries(p_ent, *p_header, *b_header, 2048, 2048,
                                       "Valid", GetBasicDataGuid()));

  // Manually corrupt entry 1 with inverted starting/ending LBA.
  GptPartitionEntry* entry1 =
      reinterpret_cast<GptPartitionEntry*>(&p_ent[sizeof(GptPartitionEntry)]);
  std::memcpy(entry1->type_guid, GetBasicDataGuid(), 16);
  entry1->starting_lba = 5000;
  entry1->ending_lba = 4000;  // Inverted!

  std::vector<PartitionInfo> partitions;
  std::vector<FreeSpaceRange> free_ranges;
  EXPECT(true, ParseGptPartitions(p_hdr.data(), p_ent.data(), kTestDiskSectors,
                                 kSectorSize, partitions, free_ranges));
  // Only the 1 valid entry should be parsed.
  EXPECT((size_t)1, partitions.size());

  // Corrupt header signature.
  p_hdr[0] = 0;
  EXPECT(false, ParseGptPartitions(p_hdr.data(), p_ent.data(), kTestDiskSectors,
                                  kSectorSize, partitions, free_ranges));
}

TEST(GptGuidFormattingAndNames) {
  const uint8_t* bios_guid = GetBiosBootGuid();
  EXPECT(std::string("21686148-6449-6E6F-744E-656564454649"),
         FormatGuid(bios_guid));
  EXPECT(std::string("BIOS Boot Partition"), GetGptTypeName(bios_guid));

  const uint8_t* basic_guid = GetBasicDataGuid();
  EXPECT(std::string("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"),
         FormatGuid(basic_guid));
  EXPECT(std::string("Basic Data (exFAT)"), GetGptTypeName(basic_guid));

  uint8_t unknown_guid[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                              0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
  EXPECT(std::string("Data Partition"), GetGptTypeName(unknown_guid));
}

TEST(GptSchemeSwitchingToMbr) {
  std::vector<uint8_t> pmbr = CreateProtectiveMbrSector(kTestDiskSectors);
  std::vector<uint8_t> p_hdr, p_ent, b_hdr, b_ent;
  CreateEmptyGptStructures(kTestDiskSectors, kSectorSize, p_hdr, p_ent, b_hdr,
                           b_ent);
  EXPECT(true, DetectGpt(pmbr.data(), p_hdr.data()));

  std::vector<uint8_t> mbr_sector0 = CreateEmptyMbrSector();
  AddMbrPartitionToSector(mbr_sector0.data(), 2048, 50000, 0x07);
  std::vector<uint8_t> zero_sector1(kSectorSize, 0);

  EXPECT(false, DetectGpt(mbr_sector0.data(), zero_sector1.data()));
  EXPECT(true, DetectMbr(mbr_sector0.data(), kTestDiskSectors));
}

}  // namespace
