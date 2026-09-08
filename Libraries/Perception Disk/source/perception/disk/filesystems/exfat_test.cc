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

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "perception/disk/formatter.h"
#include "testing.h"

namespace {

// Standard sector size in bytes.
constexpr uint32_t kSectorSize = 512;

// Standard test sector count (2 MiB volume = 4096 sectors).
constexpr uint64_t kTestSectorCount = 4096;

// Minimum allowed sector count for exFAT formatting.
constexpr uint64_t kMinimumExfatSectors = 2048;

using ::perception::disk::filesystems::FormatExfat;

TEST(ExfatValidationMinimumSectors) {
  auto dummy_writer = [](uint64_t offset, size_t bytes, const void* src) {
    return true;
  };

  EXPECT(false, FormatExfat(0, 0, kSectorSize, "TEST", dummy_writer));
  EXPECT(false, FormatExfat(0, 100, kSectorSize, "TEST", dummy_writer));
  EXPECT(false,
         FormatExfat(0, kMinimumExfatSectors - 1, kSectorSize, "TEST",
                     dummy_writer));
  EXPECT(true, FormatExfat(0, kMinimumExfatSectors, kSectorSize, "TEST",
                           dummy_writer));
}

TEST(ExfatWriterFailurePropagation) {
  for (int fail_at_call = 1; fail_at_call <= 6; fail_at_call++) {
    int call_count = 0;
    auto failing_writer = [&call_count, fail_at_call](uint64_t offset,
                                                      size_t bytes,
                                                      const void* src) -> bool {
      call_count++;
      if (call_count == fail_at_call) return false;
      return true;
    };

    EXPECT(false, FormatExfat(0, kTestSectorCount, kSectorSize, "FAILTEST",
                              failing_writer));
  }
}

TEST(ExfatStructureAndChecksums) {
  std::vector<uint8_t> disk_data(kTestSectorCount * kSectorSize, 0);

  auto writer = [&disk_data](uint64_t offset, size_t bytes,
                             const void* src) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(&disk_data[offset], src, bytes);
    return true;
  };

  EXPECT(true, FormatExfat(0, kTestSectorCount, kSectorSize, "MYEXFAT", writer));

  // Verify Sector 0: Boot Sector.
  EXPECT((uint8_t)0xEB, disk_data[0]);
  EXPECT((uint8_t)0x76, disk_data[1]);
  EXPECT((uint8_t)0x90, disk_data[2]);
  EXPECT(0, std::memcmp(&disk_data[3], "EXFAT   ", 8));
  EXPECT((uint8_t)0x55, disk_data[510]);
  EXPECT((uint8_t)0xAA, disk_data[511]);

  uint64_t vol_len = *reinterpret_cast<uint64_t*>(&disk_data[72]);
  EXPECT(kTestSectorCount, vol_len);

  uint8_t sector_shift = disk_data[108];
  uint8_t cluster_shift = disk_data[109];
  EXPECT((uint8_t)9, sector_shift);
  EXPECT((uint8_t)3, cluster_shift);

  // Verify Extended Boot Sectors (1..8) and OEM Parameter Sector (9).
  for (int s = 1; s <= 9; s++) {
    EXPECT((uint8_t)0x55, disk_data[s * kSectorSize + 510]);
    EXPECT((uint8_t)0xAA, disk_data[s * kSectorSize + 511]);
  }

  // Verify Checksum Sector (11) repeats non-zero checksum 128 times.
  uint32_t first_checksum =
      *reinterpret_cast<uint32_t*>(&disk_data[11 * kSectorSize]);
  EXPECT(false, first_checksum == 0);
  for (int i = 1; i < 128; i++) {
    uint32_t chk =
        *reinterpret_cast<uint32_t*>(&disk_data[11 * kSectorSize + i * 4]);
    EXPECT(first_checksum, chk);
  }

  // Verify Backup Boot Region (Sectors 12..23) is identical to Main Boot Region.
  EXPECT(0, std::memcmp(&disk_data[0], &disk_data[12 * kSectorSize],
                        12 * kSectorSize));

  // Verify FAT media type and end-of-chain entries.
  uint32_t fat_offset_sectors =
      *reinterpret_cast<uint32_t*>(&disk_data[80]);
  const uint32_t* fat = reinterpret_cast<const uint32_t*>(
      &disk_data[fat_offset_sectors * kSectorSize]);
  EXPECT((uint32_t)0xFFFFFFF8, fat[0]);
  EXPECT((uint32_t)0xFFFFFFFF, fat[1]);
  EXPECT((uint32_t)0xFFFFFFFF, fat[2]);  // Bitmap
  EXPECT((uint32_t)0xFFFFFFFF, fat[3]);  // Upcase
  EXPECT((uint32_t)0xFFFFFFFF, fat[4]);  // Root dir
}

TEST(ExfatVolumeLabelsDefaultAndTruncation) {
  std::vector<uint8_t> disk_data(kTestSectorCount * kSectorSize, 0);

  auto writer = [&disk_data](uint64_t offset, size_t bytes,
                             const void* src) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(&disk_data[offset], src, bytes);
    return true;
  };

  // Test empty label defaults to PERCEPTION.
  EXPECT(true, FormatExfat(0, kTestSectorCount, kSectorSize, "", writer));
  uint32_t heap_offset = *reinterpret_cast<uint32_t*>(&disk_data[88]);
  // Root dir is cluster 4, so (4-2)*8 = 16 sectors into cluster heap.
  uint64_t root_dir_offset = (heap_offset + 16) * kSectorSize;

  // Root directory entry 2 is volume label (offset 64).
  EXPECT((uint8_t)0x83, disk_data[root_dir_offset + 64]);
  uint8_t default_len = disk_data[root_dir_offset + 65];
  EXPECT((uint8_t)10, default_len);  // "PERCEPTION" is 10 chars
  char16_t first_char =
      *reinterpret_cast<char16_t*>(&disk_data[root_dir_offset + 66]);
  EXPECT((char16_t)u'P', first_char);

  // Test label longer than 11 chars is truncated.
  EXPECT(true, FormatExfat(0, kTestSectorCount, kSectorSize,
                           "VERYLONGLABELNAME", writer));
  uint8_t trunc_len = disk_data[root_dir_offset + 65];
  EXPECT((uint8_t)11, trunc_len);
}

TEST(ExfatNonZeroStartLba) {
  uint64_t start_lba = 2048;
  uint64_t total_sectors = start_lba + kTestSectorCount;
  std::vector<uint8_t> disk_data(total_sectors * kSectorSize, 0);

  auto writer = [&disk_data](uint64_t offset, size_t bytes,
                             const void* src) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(&disk_data[offset], src, bytes);
    return true;
  };

  EXPECT(true, FormatExfat(start_lba, kTestSectorCount, kSectorSize, "OFFSETFS",
                           writer));

  // Sectors before start_lba must remain untouched (all zero).
  for (size_t i = 0; i < start_lba * kSectorSize; i += 512)
    EXPECT((uint8_t)0, disk_data[i]);

  // Sector at start_lba must have the exFAT boot sector.
  EXPECT(0,
         std::memcmp(&disk_data[start_lba * kSectorSize + 3], "EXFAT   ", 8));
}

TEST(ExfatUpdatePartitionOffsetAndChecksum) {
  uint64_t initial_lba = 2048;
  uint64_t new_lba = 4096;
  uint64_t total_sectors = new_lba + kTestSectorCount;
  std::vector<uint8_t> disk_data(total_sectors * kSectorSize, 0);

  auto reader = [&disk_data](uint64_t offset, size_t bytes,
                             void* dest) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(dest, &disk_data[offset], bytes);
    return true;
  };

  auto writer = [&disk_data](uint64_t offset, size_t bytes,
                             const void* src) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(&disk_data[offset], src, bytes);
    return true;
  };

  EXPECT(true, FormatExfat(initial_lba, kTestSectorCount, kSectorSize,
                           "SHIFTTEST", writer));

  uint64_t old_offset = *reinterpret_cast<uint64_t*>(
      &disk_data[initial_lba * kSectorSize + 64]);
  EXPECT(initial_lba, old_offset);

  uint32_t old_checksum = *reinterpret_cast<uint32_t*>(
      &disk_data[(initial_lba + 11) * kSectorSize]);

  // Simulate shifting sectors to new_lba.
  std::memcpy(&disk_data[new_lba * kSectorSize],
              &disk_data[initial_lba * kSectorSize],
              kTestSectorCount * kSectorSize);

  // Calling with invalid sector size or corrupted region fails gracefully.
  EXPECT(false, perception::disk::filesystems::UpdateExfatPartitionOffset(
                    new_lba, 1024, reader, writer));

  // Update partition offset and checksum at new location.
  EXPECT(true, perception::disk::filesystems::UpdateExfatPartitionOffset(
                   new_lba, kSectorSize, reader, writer));

  uint64_t updated_offset =
      *reinterpret_cast<uint64_t*>(&disk_data[new_lba * kSectorSize + 64]);
  EXPECT(new_lba, updated_offset);

  uint32_t new_checksum = *reinterpret_cast<uint32_t*>(
      &disk_data[(new_lba + 11) * kSectorSize]);
  EXPECT(false, old_checksum == new_checksum);

  // Backup Boot Region must match updated Main Boot Region.
  EXPECT(0, std::memcmp(&disk_data[new_lba * kSectorSize],
                        &disk_data[(new_lba + 12) * kSectorSize],
                        12 * kSectorSize));
}

}  // namespace

