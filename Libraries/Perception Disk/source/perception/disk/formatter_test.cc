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

#include "perception/disk/formatter.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "perception/disk/filesystems.h"
#include "testing.h"

namespace {

// Standard sector size in bytes.
constexpr uint32_t kSectorSize = 512;

// Standard test sector count (2 MiB volume = 4096 sectors).
constexpr uint64_t kTestSectorCount = 4096;

using ::perception::disk::FilesystemType;
using ::perception::disk::FormatFilesystem;

TEST(FormatterDispatchesExfatSuccessfully) {
  std::vector<uint8_t> disk_data(kTestSectorCount * kSectorSize, 0);

  auto writer = [&disk_data](uint64_t offset, size_t bytes,
                             const void* src) -> bool {
    if (offset + bytes > disk_data.size()) return false;
    std::memcpy(&disk_data[offset], src, bytes);
    return true;
  };

  EXPECT(true, FormatFilesystem(FilesystemType::EXFAT, 0, kTestSectorCount,
                                kSectorSize, "TESTEXFAT", writer));
  EXPECT(0, std::memcmp(&disk_data[3], "EXFAT   ", 8));
}

TEST(FormatterExfatPropagatesFailure) {
  auto dummy_writer = [](uint64_t offset, size_t bytes, const void* src) {
    return true;
  };

  // Sector count too small for exFAT.
  EXPECT(false, FormatFilesystem(FilesystemType::EXFAT, 0, 100, kSectorSize,
                                 "FAIL", dummy_writer));

  // Writer failure propagation.
  auto failing_writer = [](uint64_t offset, size_t bytes, const void* src) {
    return false;
  };
  EXPECT(false, FormatFilesystem(FilesystemType::EXFAT, 0, kTestSectorCount,
                                 kSectorSize, "FAIL", failing_writer));
}

TEST(FormatterRejectsNonWritableTypes) {
  bool writer_called = false;
  auto writer = [&writer_called](uint64_t offset, size_t bytes,
                                const void* src) -> bool {
    writer_called = true;
    return true;
  };

  EXPECT(false, FormatFilesystem(FilesystemType::ISO9660, 0, kTestSectorCount,
                                 kSectorSize, "LABEL", writer));
  EXPECT(false, writer_called);

  EXPECT(false, FormatFilesystem(FilesystemType::RAW, 0, kTestSectorCount,
                                 kSectorSize, "LABEL", writer));
  EXPECT(false, writer_called);

  EXPECT(false, FormatFilesystem(FilesystemType::UNKNOWN, 0, kTestSectorCount,
                                 kSectorSize, "LABEL", writer));
  EXPECT(false, writer_called);

  EXPECT(false, FormatFilesystem(static_cast<FilesystemType>(999), 0,
                                 kTestSectorCount, kSectorSize, "LABEL",
                                 writer));
  EXPECT(false, writer_called);
}

}  // namespace
