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

#include "partitions.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "file_systems/file_system.h"
#include "perception/devices/storage_device.h"
#include "shared_memory_pool.h"

using ::file_systems::InitializeStorageDevice;
using ::perception::devices::StorageDevice;
using ::perception::devices::StorageDeviceReadRequest;

namespace {

// Byte offset of partition table in MBR sector.
constexpr size_t kMbrPartitionTableOffset = 446;

// Total primary partition entries in MBR.
constexpr int kMbrEntryCount = 4;

// MBR signature byte 0 at offset 510.
constexpr uint8_t kMbrSignature0 = 0x55;

// MBR signature byte 1 at offset 511.
constexpr uint8_t kMbrSignature1 = 0xAA;

// MBR partition type representing GPT Protective MBR.
constexpr uint8_t kMbrTypeGptProtective = 0xEE;

// Standard GPT signature "EFI PART" in little-endian uint64.
constexpr uint64_t kGptSignature = 0x5452415020494645ULL;

// Default sector size in bytes if not specified by the device.
constexpr uint64 kDefaultSectorSize = 512;

// Maximum characters in a GPT partition entry name.
constexpr size_t kGptNameMaxChars = 36;

struct __attribute__((packed)) MbrPartitionEntry {
  uint8_t boot_indicator;
  uint8_t starting_chs[3];
  uint8_t partition_type;
  uint8_t ending_chs[3];
  uint32_t starting_lba;
  uint32_t sector_count;
};

struct __attribute__((packed)) GptHeader {
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t header_crc32;
  uint32_t reserved;
  uint64_t current_lba;
  uint64_t backup_lba;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  uint8_t disk_guid[16];
  uint64_t partition_entry_lba;
  uint32_t number_of_partition_entries;
  uint32_t size_of_partition_entry;
  uint32_t partition_entry_array_crc32;
};

struct __attribute__((packed)) GptPartitionEntry {
  uint8_t partition_type_guid[16];
  uint8_t unique_partition_guid[16];
  uint64_t starting_lba;
  uint64_t ending_lba;
  uint64_t attributes;
  char16_t partition_name[36];
};

// Converts UTF-16LE characters into a UTF-8 string.
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

// Reads raw bytes from a storage device using pooled shared memory.
bool ReadDeviceBytes(StorageDevice::Client& storage_device, uint64 offset,
                     size_t bytes, void* dest) {
  auto pooled_shared_memory = kSharedMemoryPool.GetSharedMemory();
  char* buffer = reinterpret_cast<char*>(**pooled_shared_memory->shared_memory);
  size_t bytes_copied = 0;
  char* dest_bytes = static_cast<char*>(dest);

  while (bytes_copied < bytes) {
    size_t chunk = std::min(bytes - bytes_copied,
                            static_cast<size_t>(::perception::kPageSize));
    StorageDeviceReadRequest read_request;
    read_request.offset_on_device = offset + bytes_copied;
    read_request.offset_in_buffer = 0;
    read_request.bytes_to_copy = chunk;
    read_request.buffer = pooled_shared_memory->shared_memory;

    auto status = storage_device.Read(read_request);
    if (status != Status::OK) {
      kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
      return false;
    }
    std::memcpy(dest_bytes + bytes_copied, buffer, chunk);
    bytes_copied += chunk;
  }
  kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
  return true;
}

}  // namespace

std::vector<Partition> ReadPartitions(
    uint64 sector_size,
    const std::function<bool(uint64 offset, size_t bytes, void* dest)>&
        read_bytes) {
  std::vector<Partition> partitions;

  if (sector_size == 0) sector_size = kDefaultSectorSize;

  std::vector<uint8_t> sector0(sector_size, 0);
  if (!read_bytes(0, sector_size, sector0.data())) return partitions;

  if (sector0.size() < 512 || sector0[510] != kMbrSignature0 ||
      sector0[511] != kMbrSignature1)
    return partitions;

  const auto* mbr_entries = reinterpret_cast<const MbrPartitionEntry*>(
      &sector0[kMbrPartitionTableOffset]);

  if (mbr_entries[0].partition_type == kMbrTypeGptProtective) {
    std::vector<uint8_t> sector1(sector_size, 0);
    if (!read_bytes(sector_size, sector_size, sector1.data()))
      return partitions;

    const auto* gpt_hdr = reinterpret_cast<const GptHeader*>(sector1.data());
    if (gpt_hdr->signature != kGptSignature ||
        gpt_hdr->size_of_partition_entry < sizeof(GptPartitionEntry))
      return partitions;

    size_t entries_bytes = gpt_hdr->number_of_partition_entries *
                           gpt_hdr->size_of_partition_entry;
    std::vector<uint8_t> entries_buf(entries_bytes, 0);
    if (!read_bytes(gpt_hdr->partition_entry_lba * sector_size,
                    entries_bytes, entries_buf.data()))
      return partitions;

    for (uint32_t i = 0; i < gpt_hdr->number_of_partition_entries; i++) {
      const auto* entry = reinterpret_cast<const GptPartitionEntry*>(
          &entries_buf[i * gpt_hdr->size_of_partition_entry]);

      bool is_zero_guid = true;
      for (int b = 0; b < 16; b++) {
        if (entry->partition_type_guid[b] != 0) {
          is_zero_guid = false;
          break;
        }
      }
      if (is_zero_guid || entry->starting_lba > entry->ending_lba)
        continue;

      Partition part;
      part.start_offset = entry->starting_lba * sector_size;
      part.length = (entry->ending_lba - entry->starting_lba + 1) * sector_size;
      part.name = Utf16LeToUtf8(entry->partition_name, kGptNameMaxChars);
      part.mbr_type = 0;
      partitions.push_back(std::move(part));
    }
  } else {
    for (int i = 0; i < kMbrEntryCount; i++) {
      if (mbr_entries[i].partition_type != 0 &&
          mbr_entries[i].sector_count > 0) {
        Partition part;
        part.start_offset =
            static_cast<uint64>(mbr_entries[i].starting_lba) * sector_size;
        part.length =
            static_cast<uint64>(mbr_entries[i].sector_count) * sector_size;
        part.name = "";
        part.mbr_type = mbr_entries[i].partition_type;
        partitions.push_back(std::move(part));
      }
    }
  }

  return partitions;
}

std::vector<Partition> ReadPartitions(
    StorageDevice::Client& storage_device) {
  auto status_or_device_details = storage_device.GetDeviceDetails();
  if (!status_or_device_details.Ok()) return {};

  uint64 sector_size = status_or_device_details->optimal_operation_size;
  if (sector_size == 0) sector_size = kDefaultSectorSize;

  return ReadPartitions(
      sector_size, [&](uint64 offset, size_t bytes, void* dest) {
        return ReadDeviceBytes(storage_device, offset, bytes, dest);
      });
}

void OnEachFileSystemOnDevice(
    StorageDevice::Client storage_device,
    const std::function<void(std::unique_ptr<file_systems::FileSystem>)>&
        on_each_file_system) {
  auto raw_file_system = InitializeStorageDevice(storage_device);
  if (raw_file_system) {
    on_each_file_system(std::move(raw_file_system));
    return;
  }

  auto partitions = ReadPartitions(storage_device);
  for (const auto& partition : partitions) {
    auto part_fs = InitializeStorageDevice(
        storage_device, partition.start_offset, partition.length, partition.name);
    if (part_fs)
      on_each_file_system(std::move(part_fs));
  }
}
