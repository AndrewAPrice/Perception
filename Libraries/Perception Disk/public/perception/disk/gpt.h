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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "perception/disk/disk_structures.h"

namespace perception {
namespace disk {

// GPT Header structure (92 bytes).
struct __attribute__((packed)) GptHeader {
  // Signature "EFI PART" (0x5452415020494645ULL).
  uint64_t signature;

  // GPT Revision (typically 0x00010000).
  uint32_t revision;

  // Header size in bytes (92).
  uint32_t header_size;

  // CRC32 of header with this field set to 0.
  uint32_t header_crc32;

  // Reserved field, must be 0.
  uint32_t reserved;

  // Current LBA of this header.
  uint64_t current_lba;

  // Backup LBA of the alternate header.
  uint64_t backup_lba;

  // First usable LBA for partitions.
  uint64_t first_usable_lba;

  // Last usable LBA for partitions.
  uint64_t last_usable_lba;

  // Unique disk GUID (16 bytes).
  uint8_t disk_guid[16];

  // Starting LBA of partition entries array.
  uint64_t partition_entries_lba;

  // Number of partition entries (typically 128).
  uint32_t num_partition_entries;

  // Size of each partition entry in bytes (128).
  uint32_t sizeof_partition_entry;

  // CRC32 of partition entries array.
  uint32_t partition_entries_crc32;
};

// GPT Partition Entry structure (128 bytes).
struct __attribute__((packed)) GptPartitionEntry {
  // Partition type GUID (16 bytes).
  uint8_t type_guid[16];

  // Unique partition GUID (16 bytes).
  uint8_t unique_guid[16];

  // Starting LBA (inclusive).
  uint64_t starting_lba;

  // Ending LBA (inclusive).
  uint64_t ending_lba;

  // Attribute flags.
  uint64_t attributes;

  // Partition name in UTF-16LE (up to 36 characters).
  char16_t name[36];
};

// Returns true if the provided sectors contain a valid GPT disk header.
bool DetectGpt(const uint8_t* sector0, const uint8_t* sector1);

// Parses GPT partition entries and discovers unallocated free space ranges.
bool ParseGptPartitions(const uint8_t* gpt_header_sector,
                        const uint8_t* partition_entries_buffer,
                        uint64_t total_sectors, uint32_t sector_size,
                        std::vector<PartitionInfo>& out_partitions,
                        std::vector<FreeSpaceRange>& out_free_ranges);

// Generates a protective MBR sector 0.
std::vector<uint8_t> CreateProtectiveMbrSector(uint64_t total_sectors);

// Generates an initialized primary GPT header and empty partition array.
void CreateEmptyGptStructures(uint64_t total_sectors, uint32_t sector_size,
                              std::vector<uint8_t>& out_primary_header,
                              std::vector<uint8_t>& out_primary_entries,
                              std::vector<uint8_t>& out_backup_header,
                              std::vector<uint8_t>& out_backup_entries);

// Adds a new partition entry to the GPT entries buffer and updates headers.
bool AddGptPartitionToEntries(std::vector<uint8_t>& entries_buffer,
                              GptHeader& primary_header,
                              GptHeader& backup_header, uint64_t start_lba,
                              uint64_t sector_count,
                              const std::string& name_utf8,
                              const uint8_t* type_guid = nullptr);

// Deletes a partition by index from the GPT entries buffer and updates headers.
bool DeleteGptPartitionFromEntries(std::vector<uint8_t>& entries_buffer,
                                   GptHeader& primary_header,
                                   GptHeader& backup_header,
                                   int partition_index);

// Converts a 16-byte binary GUID into a standard formatted string.
std::string FormatGuid(const uint8_t* guid);

// Returns a descriptive name for a known GPT partition type GUID.
std::string GetGptTypeName(const uint8_t* type_guid);

// Returns the 16-byte GUID for a BIOS Boot Partition
// (21686148-6449-6E6F-744E-656564454649).
const uint8_t* GetBiosBootGuid();

// Returns the 16-byte GUID for Microsoft Basic Data (exFAT/NTFS)
// (EBD0A0A2-B9E5-4433-87C0-68B6B72699C7).
const uint8_t* GetBasicDataGuid();

// Returns whether the partition matches the BIOS Boot Partition type GUID.
bool IsGptBiosBootPartition(const PartitionInfo& partition);

}  // namespace disk
}  // namespace perception
