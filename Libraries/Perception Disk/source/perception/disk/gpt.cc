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

#include <algorithm>
#include <cstring>

#include "perception/disk/crc32.h"
#include "perception/random.h"

namespace {

// Standard GPT signature "EFI PART" in little-endian uint64.
constexpr uint64_t kGptSignature = 0x5452415020494645ULL;

// Standard GPT revision 1.0.
constexpr uint32_t kGptRevision = 0x00010000;

// Standard size of GPT header in bytes.
constexpr uint32_t kGptHeaderSize = 92;

// Standard number of partition entries in table.
constexpr uint32_t kGptEntryCount = 128;

// Size in bytes of each partition entry.
constexpr uint32_t kGptEntrySize = 128;

// Standard 1 MiB alignment offset in 512-byte sectors.
constexpr uint64_t kDefaultAlignmentSectors = 2048;

// Microsoft Basic Data Partition Type GUID:
// EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
constexpr uint8_t kBasicDataGuid[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9,
                                        0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6,
                                        0xB7, 0x26, 0x99, 0xC7};

// EFI System Partition Type GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
constexpr uint8_t kEfiSystemGuid[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8,
                                        0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0,
                                        0xC9, 0x3E, 0xC9, 0x3B};

// Linux Filesystem Data GUID: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
constexpr uint8_t kLinuxDataGuid[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84,
                                        0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69,
                                        0xD8, 0x47, 0x7D, 0xE4};

// BIOS Boot Partition GUID: 21686148-6449-6E6F-744E-656564454649
constexpr uint8_t kBiosBootGuid[16] = {0x48, 0x61, 0x68, 0x21, 0x49, 0x64,
                                       0x6F, 0x6E, 0x74, 0x4E, 0x65, 0x65,
                                       0x64, 0x45, 0x46, 0x49};

// Generates a random 16-byte UUID version 4.
void GenerateRandomGuid(uint8_t* out_guid) {
  for (int i = 0; i < 16; i += sizeof(size_t)) {
    size_t r = perception::RandomNumber();
    std::memcpy(&out_guid[i], &r, std::min<size_t>(sizeof(size_t), 16 - i));
  }
  out_guid[6] = (out_guid[6] & 0x0F) | 0x40;  // Version 4
  out_guid[8] = (out_guid[8] & 0x3F) | 0x80;  // Variant 1
}

// Converts UTF-8 string into UTF-16LE buffer.
void Utf8ToUtf16Le(const std::string& utf8, char16_t* out_utf16,
                   size_t max_chars) {
  size_t in_idx = 0;
  size_t out_idx = 0;
  while (in_idx < utf8.size() && out_idx + 1 < max_chars) {
    uint8_t c = static_cast<uint8_t>(utf8[in_idx]);
    if (c < 0x80) {
      out_utf16[out_idx++] = static_cast<char16_t>(c);
      in_idx++;
    } else if ((c & 0xE0) == 0xC0 && in_idx + 1 < utf8.size()) {
      char16_t ch = ((c & 0x1F) << 6) | (utf8[in_idx + 1] & 0x3F);
      out_utf16[out_idx++] = ch;
      in_idx += 2;
    } else {
      out_utf16[out_idx++] = u'?';
      in_idx++;
    }
  }
  out_utf16[out_idx] = 0;
}

// Converts UTF-16LE buffer into UTF-8 string.
std::string Utf16LeToUtf8(const char16_t* utf16, size_t max_chars) {
  std::string utf8;
  for (size_t i = 0; i < max_chars && utf16[i] != 0; i++) {
    char16_t ch = utf16[i];
    if (ch < 0x80) {
      utf8.push_back(static_cast<char>(ch));
    } else if (ch < 0x800) {
      utf8.push_back(static_cast<char>(0xC0 | (ch >> 6)));
      utf8.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else {
      utf8.push_back('?');
    }
  }
  return utf8;
}

// Checks if a 16-byte GUID is all zeros.
bool IsGuidEmpty(const uint8_t* guid) {
  for (int i = 0; i < 16; i++) {
    if (guid[i] != 0) return false;
  }
  return true;
}

}  // namespace

namespace perception {
namespace disk {

bool DetectGpt(const uint8_t* sector0, const uint8_t* sector1) {
  if (sector0 != nullptr) {
    if (sector0[510] != 0x55 || sector0[511] != 0xAA) return false;
    uint8_t partition_type = sector0[446 + 4];
    if (partition_type != 0xEE) return false;
  }

  if (sector1 == nullptr) return false;

  const GptHeader* header = reinterpret_cast<const GptHeader*>(sector1);
  return header->signature == kGptSignature;
}

bool ParseGptPartitions(const uint8_t* gpt_header_sector,
                        const uint8_t* partition_entries_buffer,
                        uint64_t total_sectors, uint32_t sector_size,
                        std::vector<PartitionInfo>& out_partitions,
                        std::vector<FreeSpaceRange>& out_free_ranges) {
  out_partitions.clear();
  out_free_ranges.clear();

  const GptHeader* header =
      reinterpret_cast<const GptHeader*>(gpt_header_sector);
  if (header->signature != kGptSignature) return false;

  uint32_t num_entries = header->num_partition_entries;
  uint32_t entry_size = header->sizeof_partition_entry;
  if (entry_size < sizeof(GptPartitionEntry))
    entry_size = sizeof(GptPartitionEntry);

  struct Extent {
    uint64_t start;
    uint64_t end;
  };
  std::vector<Extent> extents;

  int partition_idx = 1;
  for (uint32_t i = 0; i < num_entries; i++) {
    const GptPartitionEntry* entry = reinterpret_cast<const GptPartitionEntry*>(
        &partition_entries_buffer[i * entry_size]);

    if (IsGuidEmpty(entry->type_guid) || entry->starting_lba == 0 ||
        entry->ending_lba < entry->starting_lba)
      continue;

    PartitionInfo info;
    info.partition_number = partition_idx++;
    info.type_guid = FormatGuid(entry->type_guid);
    info.type_name = GetGptTypeName(entry->type_guid);
    info.name = Utf16LeToUtf8(entry->name, 36);
    if (info.name.empty())
      info.name = "Partition " + std::to_string(info.partition_number);

    info.start_lba = entry->starting_lba;
    info.end_lba = entry->ending_lba;
    info.sector_count = info.end_lba - info.start_lba + 1;
    info.size_in_bytes = info.sector_count * sector_size;

    out_partitions.push_back(info);
    extents.push_back({info.start_lba, info.end_lba});
  }

  std::sort(extents.begin(), extents.end(),
            [](const Extent& a, const Extent& b) { return a.start < b.start; });

  uint64_t first_usable = header->first_usable_lba;
  if (first_usable < kDefaultAlignmentSectors)
    first_usable = kDefaultAlignmentSectors;
  uint64_t last_usable = header->last_usable_lba;

  uint64_t current_lba = first_usable;
  for (const auto& ext : extents) {
    if (ext.start > current_lba) {
      FreeSpaceRange range;
      range.start_lba = current_lba;
      range.end_lba = ext.start - 1;
      range.sector_count = range.end_lba - range.start_lba + 1;
      range.size_in_bytes = range.sector_count * sector_size;
      if (range.sector_count >= kDefaultAlignmentSectors)
        out_free_ranges.push_back(range);
    }
    if (ext.end + 1 > current_lba) current_lba = ext.end + 1;
  }

  if (current_lba <= last_usable) {
    FreeSpaceRange range;
    range.start_lba = current_lba;
    range.end_lba = last_usable;
    range.sector_count = range.end_lba - range.start_lba + 1;
    range.size_in_bytes = range.sector_count * sector_size;
    if (range.sector_count > 0) out_free_ranges.push_back(range);
  }

  return true;
}

std::vector<uint8_t> CreateProtectiveMbrSector(uint64_t total_sectors) {
  std::vector<uint8_t> sector(512, 0);
  // Partition entry 0 at offset 446 (0x1BE)
  sector[446] = 0x00;  // Non-bootable
  sector[447] = 0x00;  // Starting CHS
  sector[448] = 0x02;
  sector[449] = 0x00;
  sector[450] = 0xEE;  // GPT Protective MBR type
  sector[451] = 0xFF;  // Ending CHS
  sector[452] = 0xFF;
  sector[453] = 0xFF;

  uint32_t starting_lba = 1;
  uint32_t sector_count = static_cast<uint32_t>(
      std::min<uint64_t>(total_sectors - 1, 0xFFFFFFFFULL));
  std::memcpy(&sector[454], &starting_lba, 4);
  std::memcpy(&sector[458], &sector_count, 4);

  sector[510] = 0x55;
  sector[511] = 0xAA;
  return sector;
}

void CreateEmptyGptStructures(uint64_t total_sectors, uint32_t sector_size,
                              std::vector<uint8_t>& out_primary_header,
                              std::vector<uint8_t>& out_primary_entries,
                              std::vector<uint8_t>& out_backup_header,
                              std::vector<uint8_t>& out_backup_entries) {
  out_primary_header.assign(sector_size, 0);
  out_backup_header.assign(sector_size, 0);

  size_t entries_bytes = kGptEntryCount * kGptEntrySize;
  out_primary_entries.assign(entries_bytes, 0);
  out_backup_entries.assign(entries_bytes, 0);

  uint32_t entries_crc =
      CalculateCrc32(out_primary_entries.data(), entries_bytes);

  uint8_t disk_guid[16];
  GenerateRandomGuid(disk_guid);

  uint64_t entries_sectors = (entries_bytes + sector_size - 1) / sector_size;
  uint64_t first_usable = 2 + entries_sectors;
  if (first_usable < kDefaultAlignmentSectors)
    first_usable = kDefaultAlignmentSectors;
  uint64_t last_usable = total_sectors - entries_sectors - 2;

  GptHeader primary = {};
  primary.signature = kGptSignature;
  primary.revision = kGptRevision;
  primary.header_size = kGptHeaderSize;
  primary.header_crc32 = 0;
  primary.reserved = 0;
  primary.current_lba = 1;
  primary.backup_lba = total_sectors - 1;
  primary.first_usable_lba = first_usable;
  primary.last_usable_lba = last_usable;
  std::memcpy(primary.disk_guid, disk_guid, 16);
  primary.partition_entries_lba = 2;
  primary.num_partition_entries = kGptEntryCount;
  primary.sizeof_partition_entry = kGptEntrySize;
  primary.partition_entries_crc32 = entries_crc;

  primary.header_crc32 = CalculateCrc32(
      reinterpret_cast<const uint8_t*>(&primary), kGptHeaderSize);
  std::memcpy(out_primary_header.data(), &primary, sizeof(GptHeader));

  GptHeader backup = primary;
  backup.current_lba = total_sectors - 1;
  backup.backup_lba = 1;
  backup.partition_entries_lba = total_sectors - 1 - entries_sectors;
  backup.header_crc32 = 0;
  backup.header_crc32 =
      CalculateCrc32(reinterpret_cast<const uint8_t*>(&backup), kGptHeaderSize);
  std::memcpy(out_backup_header.data(), &backup, sizeof(GptHeader));
}

bool AddGptPartitionToEntries(std::vector<uint8_t>& entries_buffer,
                              GptHeader& primary_header,
                              GptHeader& backup_header, uint64_t start_lba,
                              uint64_t sector_count,
                              const std::string& name_utf8,
                              const uint8_t* type_guid) {
  uint32_t num_entries = primary_header.num_partition_entries;
  uint32_t entry_size = primary_header.sizeof_partition_entry;

  for (uint32_t i = 0; i < num_entries; i++) {
    GptPartitionEntry* entry =
        reinterpret_cast<GptPartitionEntry*>(&entries_buffer[i * entry_size]);
    if (IsGuidEmpty(entry->type_guid)) {
      std::memset(entry, 0, entry_size);
      if (type_guid != nullptr)
        std::memcpy(entry->type_guid, type_guid, 16);
      else
        std::memcpy(entry->type_guid, kBasicDataGuid, 16);

      GenerateRandomGuid(entry->unique_guid);
      entry->starting_lba = start_lba;
      entry->ending_lba = start_lba + sector_count - 1;
      entry->attributes = 0;
      Utf8ToUtf16Le(name_utf8, entry->name, 36);

      uint32_t entries_crc =
          CalculateCrc32(entries_buffer.data(), entries_buffer.size());
      primary_header.partition_entries_crc32 = entries_crc;
      backup_header.partition_entries_crc32 = entries_crc;

      primary_header.header_crc32 = 0;
      primary_header.header_crc32 = CalculateCrc32(
          reinterpret_cast<const uint8_t*>(&primary_header), kGptHeaderSize);

      backup_header.header_crc32 = 0;
      backup_header.header_crc32 = CalculateCrc32(
          reinterpret_cast<const uint8_t*>(&backup_header), kGptHeaderSize);

      return true;
    }
  }
  return false;
}

bool DeleteGptPartitionFromEntries(std::vector<uint8_t>& entries_buffer,
                                   GptHeader& primary_header,
                                   GptHeader& backup_header,
                                   int partition_index) {
  uint32_t entry_size = primary_header.sizeof_partition_entry;
  if (partition_index < 0 || static_cast<size_t>(partition_index) >=
                                 primary_header.num_partition_entries)
    return false;

  GptPartitionEntry* entry = reinterpret_cast<GptPartitionEntry*>(
      &entries_buffer[partition_index * entry_size]);
  std::memset(entry, 0, entry_size);

  uint32_t entries_crc =
      CalculateCrc32(entries_buffer.data(), entries_buffer.size());
  primary_header.partition_entries_crc32 = entries_crc;
  backup_header.partition_entries_crc32 = entries_crc;

  primary_header.header_crc32 = 0;
  primary_header.header_crc32 = CalculateCrc32(
      reinterpret_cast<const uint8_t*>(&primary_header), kGptHeaderSize);

  backup_header.header_crc32 = 0;
  backup_header.header_crc32 = CalculateCrc32(
      reinterpret_cast<const uint8_t*>(&backup_header), kGptHeaderSize);

  return true;
}

std::string FormatGuid(const uint8_t* guid) {
  char buf[40];
  uint32_t d1 = *reinterpret_cast<const uint32_t*>(&guid[0]);
  uint16_t d2 = *reinterpret_cast<const uint16_t*>(&guid[4]);
  uint16_t d3 = *reinterpret_cast<const uint16_t*>(&guid[6]);
  std::snprintf(buf, sizeof(buf),
                "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", d1, d2, d3,
                guid[8], guid[9], guid[10], guid[11], guid[12], guid[13],
                guid[14], guid[15]);
  return std::string(buf);
}

std::string GetGptTypeName(const uint8_t* type_guid) {
  if (std::memcmp(type_guid, kBiosBootGuid, 16) == 0)
    return "BIOS Boot Partition";
  if (std::memcmp(type_guid, kBasicDataGuid, 16) == 0)
    return "Basic Data (exFAT)";
  if (std::memcmp(type_guid, kEfiSystemGuid, 16) == 0)
    return "EFI System Partition";
  if (std::memcmp(type_guid, kLinuxDataGuid, 16) == 0)
    return "Linux Filesystem";
  return "Data Partition";
}

const uint8_t* GetBiosBootGuid() { return kBiosBootGuid; }

const uint8_t* GetBasicDataGuid() { return kBasicDataGuid; }

bool IsGptBiosBootPartition(const PartitionInfo& partition) {
  return partition.type_name == "BIOS Boot Partition" ||
         partition.type_guid == "21686148-6449-6E6F-744E-656564454649";
}

}  // namespace disk
}  // namespace perception
