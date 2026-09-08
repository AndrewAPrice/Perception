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

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "perception/disk/disk_structures.h"
#include "perception/disk/filesystems/exfat.h"
#include "testing.h"

namespace {

// Standard sector size in bytes.
constexpr uint32_t kSectorSize = 512;

// Optical disc sector size in bytes.
constexpr uint32_t kOpticalSectorSize = 2048;

// Primary Volume Descriptor sector index for ISO 9660.
constexpr uint64_t kIso9660PvdSector = 16;

// Standard test sector count (2 MiB volume = 4096 sectors).
constexpr uint64_t kTestSectorCount = 4096;

using ::perception::disk::AnalyzeFilesystem;
using ::perception::disk::AnalyzeRawDevice;
using ::perception::disk::DiskInfo;
using ::perception::disk::FilesystemType;
using ::perception::disk::PartitionInfo;
using ::perception::disk::filesystems::FormatExfat;

TEST(AnalyzeFilesystemReaderFailureYieldsUnknown) {
  PartitionInfo part;
  part.size_in_bytes = 1048576;

  auto failing_reader = [](uint64_t offset, size_t bytes, void* dest) -> bool {
    return false;
  };

  AnalyzeFilesystem(0, 2048, kSectorSize, failing_reader, part);
  EXPECT((int)FilesystemType::UNKNOWN, (int)part.filesystem_type);
  EXPECT(std::string("Unknown"), part.filesystem_name);
  EXPECT((uint64)0, part.used_bytes);
  EXPECT((uint64)1048576, part.free_bytes);
}

TEST(AnalyzeFilesystemUnformattedYieldsRaw) {
  std::vector<uint8_t> zero_disk(kSectorSize, 0);
  PartitionInfo part;
  part.size_in_bytes = 1048576;

  auto reader = [&zero_disk](uint64_t offset, size_t bytes,
                             void* dest) -> bool {
    if (offset + bytes > zero_disk.size()) return false;
    std::memcpy(dest, &zero_disk[offset], bytes);
    return true;
  };

  AnalyzeFilesystem(0, 2048, kSectorSize, reader, part);
  EXPECT((int)FilesystemType::RAW, (int)part.filesystem_type);
  EXPECT(std::string("Unformatted"), part.filesystem_name);
  EXPECT((uint64)0, part.used_bytes);
  EXPECT((uint64)1048576, part.free_bytes);
}

TEST(AnalyzeFilesystemValidExfat) {
  std::vector<uint8_t> disk_data(kTestSectorCount * kSectorSize, 0);

  auto writer = [&disk_data](uint64_t offset, size_t bytes,
                             const void* src) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(&disk_data[offset], src, bytes);
    return true;
  };

  EXPECT(true, FormatExfat(0, kTestSectorCount, kSectorSize, "TESTVOL", writer));

  auto reader = [&disk_data](uint64_t offset, size_t bytes,
                             void* dest) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(dest, &disk_data[offset], bytes);
    return true;
  };

  PartitionInfo part;
  part.start_lba = 0;
  part.sector_count = kTestSectorCount;
  part.size_in_bytes = kTestSectorCount * kSectorSize;

  AnalyzeFilesystem(0, kTestSectorCount, kSectorSize, reader, part);
  EXPECT((int)FilesystemType::EXFAT, (int)part.filesystem_type);
  EXPECT(std::string("exFAT"), part.filesystem_name);
  EXPECT(std::string("TESTVOL"), part.name);
  EXPECT(false, part.used_bytes == 0);
  EXPECT(false, part.free_bytes == 0);
  EXPECT(true, part.used_bytes + part.free_bytes <= part.size_in_bytes);
}

TEST(AnalyzeFilesystemExfatMalformedShiftLeavesVolumeUnanalyzed) {
  std::vector<uint8_t> disk_data(kSectorSize, 0);
  std::memcpy(&disk_data[3], "EXFAT   ", 8);
  disk_data[510] = 0x55;
  disk_data[511] = 0xAA;
  disk_data[108] = 8;  // Sector shift < 9 is invalid for exFAT

  auto reader = [&disk_data](uint64_t offset, size_t bytes,
                             void* dest) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(dest, &disk_data[offset], bytes);
    return true;
  };

  PartitionInfo part;
  part.size_in_bytes = 1048576;
  part.used_bytes = 0;
  part.free_bytes = 0;
  AnalyzeFilesystem(0, 2048, kSectorSize, reader, part);
  EXPECT((int)FilesystemType::EXFAT, (int)part.filesystem_type);
  EXPECT((uint64)0, part.used_bytes);
  EXPECT((uint64)0, part.free_bytes);
}

TEST(AnalyzeRawDeviceExfatAndRaw) {
  std::vector<uint8_t> disk_data(kTestSectorCount * kSectorSize, 0);

  auto writer = [&disk_data](uint64_t offset, size_t bytes,
                             const void* src) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(&disk_data[offset], src, bytes);
    return true;
  };

  EXPECT(true, FormatExfat(0, kTestSectorCount, kSectorSize, "RAWEXFAT", writer));

  auto reader = [&disk_data](uint64_t offset, size_t bytes,
                             void* dest) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(dest, &disk_data[offset], bytes);
    return true;
  };

  DiskInfo disk;
  disk.total_sectors = kTestSectorCount;
  disk.sector_size = kSectorSize;
  disk.size_in_bytes = kTestSectorCount * kSectorSize;

  AnalyzeRawDevice(kTestSectorCount, kSectorSize, reader, disk);
  EXPECT((int)FilesystemType::EXFAT, (int)disk.raw_filesystem);
  EXPECT(false, disk.raw_used_bytes == 0);

  // Test unformatted disk yields RAW.
  std::vector<uint8_t> unformatted(kSectorSize, 0);
  auto unformatted_reader = [&unformatted](uint64_t offset, size_t bytes,
                                          void* dest) -> bool {
    if (offset + bytes > unformatted.size()) return false;
    std::memcpy(dest, &unformatted[offset], bytes);
    return true;
  };

  DiskInfo unformatted_disk;
  unformatted_disk.size_in_bytes = 1048576;
  AnalyzeRawDevice(2048, kSectorSize, unformatted_reader, unformatted_disk);
  EXPECT((int)FilesystemType::RAW, (int)unformatted_disk.raw_filesystem);
  EXPECT((uint64)0, unformatted_disk.raw_used_bytes);
  EXPECT((uint64)1048576, unformatted_disk.raw_free_bytes);
}

TEST(AnalyzeRawDeviceIso9660) {
  size_t iso_size = (kIso9660PvdSector + 2) * kOpticalSectorSize;
  std::vector<uint8_t> iso_data(iso_size, 0);

  uint64_t pvd_offset = kIso9660PvdSector * kOpticalSectorSize;
  uint8_t* pvd = &iso_data[pvd_offset];
  pvd[0] = 1;  // Primary Volume Descriptor type
  std::memcpy(&pvd[1], "CD001", 5);

  std::string vol_id = "PERCEPTION_INSTALLER         ";
  std::memcpy(&pvd[40], vol_id.data(), 32);

  uint32_t volume_space_size = 50000;
  uint16_t logical_block_size = 2048;
  std::memcpy(&pvd[80], &volume_space_size, 4);
  std::memcpy(&pvd[128], &logical_block_size, 2);

  auto reader = [&iso_data](uint64_t offset, size_t bytes,
                            void* dest) -> bool {
    if (offset + bytes > iso_data.size()) return false;
    std::memcpy(dest, &iso_data[offset], bytes);
    return true;
  };

  DiskInfo disk;
  disk.total_sectors = 50000;
  disk.sector_size = kOpticalSectorSize;
  disk.size_in_bytes = static_cast<uint64_t>(50000) * kOpticalSectorSize;

  AnalyzeRawDevice(50000, kOpticalSectorSize, reader, disk);
  EXPECT((int)FilesystemType::ISO9660, (int)disk.raw_filesystem);
  EXPECT((uint64)(50000ULL * 2048), disk.raw_used_bytes);
  EXPECT((uint64)0, disk.raw_free_bytes);
  EXPECT(std::string("PERCEPTION_INSTALLER"), disk.name);
}

TEST(AnalyzeRawDeviceCorruptedIsoAndSector0Failure) {
  size_t iso_size = (kIso9660PvdSector + 2) * kOpticalSectorSize;
  std::vector<uint8_t> corrupted_iso(iso_size, 0);

  uint64_t pvd_offset = kIso9660PvdSector * kOpticalSectorSize;
  corrupted_iso[pvd_offset] = 2;  // Not type 1
  std::memcpy(&corrupted_iso[pvd_offset + 1], "CD001", 5);

  auto corrupted_reader = [&corrupted_iso](uint64_t offset, size_t bytes,
                                          void* dest) -> bool {
    if (offset + bytes > corrupted_iso.size()) return false;
    std::memcpy(dest, &corrupted_iso[offset], bytes);
    return true;
  };

  DiskInfo disk;
  disk.size_in_bytes = 1000000;
  AnalyzeRawDevice(1000, kOpticalSectorSize, corrupted_reader, disk);
  EXPECT((int)FilesystemType::RAW, (int)disk.raw_filesystem);

  // Sector 0 read failure should exit without modifying disk fields.
  auto failing_reader = [](uint64_t offset, size_t bytes, void* dest) -> bool {
    return false;
  };
  DiskInfo untouched_disk;
  untouched_disk.raw_filesystem = FilesystemType::UNKNOWN;
  AnalyzeRawDevice(1000, kSectorSize, failing_reader, untouched_disk);
  EXPECT((int)FilesystemType::UNKNOWN, (int)untouched_disk.raw_filesystem);
}

}  // namespace
