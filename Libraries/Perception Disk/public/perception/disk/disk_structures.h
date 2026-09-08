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

#include "perception/devices/storage_device.h"
#include "perception/disk/filesystems.h"

namespace perception {
namespace disk {

// Partition table scheme of a storage device.
enum class PartitionScheme { NONE, MBR, GPT };

// Details of an unpartitioned free space span on disk.
struct FreeSpaceRange {
  // Starting logical block address.
  uint64_t start_lba = 0;

  // Ending logical block address (inclusive).
  uint64_t end_lba = 0;

  // Number of sectors in this free space range.
  uint64_t sector_count = 0;

  // Total size in bytes.
  uint64_t size_in_bytes = 0;
};

// Details of an individual partition.
struct PartitionInfo {
  // Partition index (1-based for user display).
  int partition_number = 0;

  // Label or partition name.
  std::string name;

  // Starting logical block address on the device.
  uint64_t start_lba = 0;

  // Ending logical block address (inclusive).
  uint64_t end_lba = 0;

  // Number of sectors in this partition.
  uint64_t sector_count = 0;

  // Total capacity in bytes.
  uint64_t size_in_bytes = 0;

  // Partition type GUID (for GPT) or MBR partition type byte (for MBR).
  std::string type_guid;

  // MBR partition type byte (e.g. 0x07 for exFAT/NTFS, 0xEE for GPT).
  uint8_t mbr_type = 0;

  // Descriptive type name (e.g. "Basic Data", "EFI System", "Unformatted").
  std::string type_name;

  // Detected filesystem on the partition.
  FilesystemType filesystem_type = FilesystemType::UNKNOWN;

  // Filesystem name (e.g. "exFAT", "ISO 9660", "Unformatted").
  std::string filesystem_name;

  // Used space in bytes within the filesystem.
  uint64_t used_bytes = 0;

  // Free space in bytes within the filesystem.
  uint64_t free_bytes = 0;

  // Whether this partition is currently mounted in the VFS.
  bool is_mounted = false;

  // Mount point path if mounted (e.g. "/Drive 1/").
  std::string mount_point;

  // Whether this partition is the boot drive (cannot be unmounted).
  bool is_boot_drive = false;
};

// Specification for a partition in the planned partition table editor.
struct PlannedPartition {
  // Original partition number if this was derived from an existing partition (-1 if newly added).
  int original_partition_number = -1;

  // Original starting logical block address on device.
  uint64_t original_start_lba = 0;

  // Original sector count.
  uint64_t original_sector_count = 0;

  // Partition name / volume label.
  std::string name;

  // Target partition capacity in bytes.
  uint64_t size_in_bytes = 0;

  // Target filesystem.
  FilesystemType filesystem_type = FilesystemType::EXFAT;

  // Whether data on this partition will be preserved untouched during partitioning.
  bool is_untouched = false;
};

// Details of a physical or virtual storage device.
struct DiskInfo {
  // Device client handle for I/O operations.
  perception::devices::StorageDevice::Client device;

  // Name of the storage device.
  std::string name;

  // Total capacity of the device in bytes.
  uint64_t size_in_bytes = 0;

  // Sector size in bytes (typically 512 or 2048).
  uint32_t sector_size = 512;

  // Total sector count.
  uint64_t total_sectors = 0;

  // Whether the device is writable.
  bool is_writable = false;

  // Storage device type (RAM, OPTICAL, HARD_DRIVE).
  perception::devices::StorageDeviceType device_type =
      perception::devices::StorageDeviceType::HARD_DRIVE;

  // Partition table scheme detected on the device.
  PartitionScheme scheme = PartitionScheme::NONE;

  // Partitions discovered on this device.
  std::vector<PartitionInfo> partitions;

  // Unallocated free space spans on this device.
  std::vector<FreeSpaceRange> free_space_ranges;

  // Filesystem on the raw disk if no partition table exists.
  FilesystemType raw_filesystem = FilesystemType::UNKNOWN;

  // Used space on raw disk.
  uint64_t raw_used_bytes = 0;

  // Free space on raw disk.
  uint64_t raw_free_bytes = 0;

  // Whether this disk is currently mounted in the VFS.
  bool is_mounted = false;

  // Mount point path if mounted (e.g. "/Optical 1/").
  std::string mount_point;

  // Whether this disk is the boot drive (cannot be unmounted).
  bool is_boot_drive = false;
};

}  // namespace disk
}  // namespace perception
