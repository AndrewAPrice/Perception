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

#include "perception/disk/filesystems.h"

#include <string>
#include <string_view>
#include <vector>

#include "testing.h"

namespace {

using ::perception::disk::FilesystemType;
using ::perception::disk::FilesystemTypeFromString;
using ::perception::disk::FilesystemTypeToString;
using ::perception::disk::GetSupportedFilesystems;
using ::perception::disk::GetWritableFilesystems;
using ::perception::disk::IsFilesystemWritable;

TEST(FilesystemTypeToStringConversions) {
  EXPECT(std::string("exFAT"),
         std::string(FilesystemTypeToString(FilesystemType::EXFAT)));
  EXPECT(std::string("ISO 9660"),
         std::string(FilesystemTypeToString(FilesystemType::ISO9660)));
  EXPECT(std::string("Raw"),
         std::string(FilesystemTypeToString(FilesystemType::RAW)));
  EXPECT(std::string("Unknown"),
         std::string(FilesystemTypeToString(FilesystemType::UNKNOWN)));

  // Fallback for unmapped or invalid enum values.
  EXPECT(std::string("Unknown"),
         std::string(FilesystemTypeToString(static_cast<FilesystemType>(999))));
}

TEST(FilesystemTypeFromStringExfatVariants) {
  EXPECT((int)FilesystemType::EXFAT, (int)FilesystemTypeFromString("exFAT"));
  EXPECT((int)FilesystemType::EXFAT, (int)FilesystemTypeFromString("exfat"));
  EXPECT((int)FilesystemType::EXFAT, (int)FilesystemTypeFromString("EXFAT"));
  EXPECT((int)FilesystemType::EXFAT, (int)FilesystemTypeFromString("ExFat"));
  EXPECT((int)FilesystemType::EXFAT, (int)FilesystemTypeFromString("  exFAT  "));
  EXPECT((int)FilesystemType::EXFAT, (int)FilesystemTypeFromString("ex-fat"));
  EXPECT((int)FilesystemType::EXFAT, (int)FilesystemTypeFromString("ex_fat"));
  EXPECT((int)FilesystemType::EXFAT, (int)FilesystemTypeFromString("EX-FAT"));
}

TEST(FilesystemTypeFromStringIsoVariants) {
  EXPECT((int)FilesystemType::ISO9660,
         (int)FilesystemTypeFromString("ISO 9660"));
  EXPECT((int)FilesystemType::ISO9660,
         (int)FilesystemTypeFromString("iso9660"));
  EXPECT((int)FilesystemType::ISO9660,
         (int)FilesystemTypeFromString("ISO9660"));
  EXPECT((int)FilesystemType::ISO9660,
         (int)FilesystemTypeFromString("iso-9660"));
  EXPECT((int)FilesystemType::ISO9660,
         (int)FilesystemTypeFromString("iso_9660"));
  EXPECT((int)FilesystemType::ISO9660, (int)FilesystemTypeFromString("iso"));
  EXPECT((int)FilesystemType::ISO9660, (int)FilesystemTypeFromString("ISO"));
  EXPECT((int)FilesystemType::ISO9660, (int)FilesystemTypeFromString("  iso  "));
}

TEST(FilesystemTypeFromStringRawVariants) {
  EXPECT((int)FilesystemType::RAW, (int)FilesystemTypeFromString("Raw"));
  EXPECT((int)FilesystemType::RAW, (int)FilesystemTypeFromString("raw"));
  EXPECT((int)FilesystemType::RAW, (int)FilesystemTypeFromString("RAW"));
  EXPECT((int)FilesystemType::RAW,
         (int)FilesystemTypeFromString("Unformatted"));
  EXPECT((int)FilesystemType::RAW,
         (int)FilesystemTypeFromString("unformatted"));
  EXPECT((int)FilesystemType::RAW,
         (int)FilesystemTypeFromString("UNFORMATTED"));
  EXPECT((int)FilesystemType::RAW,
         (int)FilesystemTypeFromString("un-formatted"));
  EXPECT((int)FilesystemType::RAW,
         (int)FilesystemTypeFromString("un_formatted"));
}

TEST(FilesystemTypeFromStringInvalidInputs) {
  EXPECT((int)FilesystemType::UNKNOWN, (int)FilesystemTypeFromString(""));
  EXPECT((int)FilesystemType::UNKNOWN, (int)FilesystemTypeFromString("   "));
  EXPECT((int)FilesystemType::UNKNOWN, (int)FilesystemTypeFromString("---"));
  EXPECT((int)FilesystemType::UNKNOWN, (int)FilesystemTypeFromString("___"));
  EXPECT((int)FilesystemType::UNKNOWN, (int)FilesystemTypeFromString("ntfs"));
  EXPECT((int)FilesystemType::UNKNOWN, (int)FilesystemTypeFromString("fat32"));
  EXPECT((int)FilesystemType::UNKNOWN, (int)FilesystemTypeFromString("ext4"));
  EXPECT((int)FilesystemType::UNKNOWN, (int)FilesystemTypeFromString("exfatty"));
  EXPECT((int)FilesystemType::UNKNOWN,
         (int)FilesystemTypeFromString("iso96600"));
}

TEST(FilesystemQueriesSupportedAndWritable) {
  const auto& supported = GetSupportedFilesystems();
  ASSERT((size_t)2, supported.size());
  EXPECT((int)FilesystemType::EXFAT, (int)supported[0]);
  EXPECT((int)FilesystemType::ISO9660, (int)supported[1]);

  const auto& writable = GetWritableFilesystems();
  ASSERT((size_t)1, writable.size());
  EXPECT((int)FilesystemType::EXFAT, (int)writable[0]);

  EXPECT(true, IsFilesystemWritable(FilesystemType::EXFAT));
  EXPECT(false, IsFilesystemWritable(FilesystemType::ISO9660));
  EXPECT(false, IsFilesystemWritable(FilesystemType::RAW));
  EXPECT(false, IsFilesystemWritable(FilesystemType::UNKNOWN));
  EXPECT(false,
         IsFilesystemWritable(static_cast<FilesystemType>(999)));
}

}  // namespace
