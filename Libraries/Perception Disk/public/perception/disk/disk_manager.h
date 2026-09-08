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

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "perception/devices/storage_device.h"
#include "perception/disk/disk_structures.h"
#include "perception/shared_memory.h"
#include "perception/shared_memory_pool.h"
#include "perception/storage_manager.h"

namespace perception {
namespace disk {

// Size in bytes of shared memory transfer buffer (64 KiB = 16 pages).
constexpr size_t kTransferBufferSize = 65536;

// Coordinates discovery, inspection, partitioning, and formatting of storage devices.
class DiskManager {
 public:
  DiskManager();

  // Initializes listeners for storage device services.
  void Initialize();

  // Returns list of discovered disks.
  const std::vector<DiskInfo>& GetDisks() const;

  // Registers callback for when disks or partitions are updated.
  void OnDisksUpdated(std::function<void()> callback);

  // Forces a re-scan of all connected storage devices and partitions.
  void RescanAll();

  // Re-scans a specific disk.
  void RescanDisk(DiskInfo& disk);

  // Initializes a disk with a fresh partition table (GPT or MBR).
  bool InitializeDiskWithScheme(DiskInfo& disk, PartitionScheme scheme);

  // Applies a full partitioning layout to a disk, shifting and preserving untouched partitions.
  bool PartitionAndFormatDisk(DiskInfo& disk, PartitionScheme scheme,
                              const std::vector<PlannedPartition>& partitions);

  // Relocates raw sector data within a disk safely without data loss.
  bool ShiftDiskSectors(DiskInfo& disk, uint64_t src_lba, uint64_t dst_lba,
                        uint64_t sector_count);

  // Creates a new partition in the specified free space range.
  bool AddPartition(DiskInfo& disk, uint64_t start_lba, uint64_t sector_count,
                    const std::string& name);

  // Deletes an existing partition from the disk's partition table.
  bool DeletePartition(DiskInfo& disk, int partition_number);

  // Formats an individual partition with the specified filesystem.
  bool FormatPartition(DiskInfo& disk, int partition_number,
                       const std::string& volume_label,
                       FilesystemType filesystem);

  // Formats an individual partition (defaults to exFAT).
  bool FormatPartition(DiskInfo& disk, int partition_number,
                       const std::string& volume_label);

  // Formats the entire raw storage device with the specified filesystem.
  bool FormatRawDisk(DiskInfo& disk, const std::string& volume_label,
                     FilesystemType filesystem);

  // Formats the entire raw storage device (defaults to exFAT).
  bool FormatRawDisk(DiskInfo& disk, const std::string& volume_label);

  // Mounts a whole raw disk or device.
  bool MountDisk(DiskInfo& disk, std::string_view target_mount_point = "");

  // Mounts all unmounted partitions on a partitioned disk.
  bool MountAllPartitions(DiskInfo& disk);

  // Mounts an individual partition on a disk.
  bool MountPartition(DiskInfo& disk, int partition_number,
                      std::string_view target_mount_point = "");

  // Unmounts a mounted volume by its mount point.
  bool Unmount(std::string_view mount_point);

  // Unmounts all mounted partitions on a partitioned disk, excluding any boot volume.
  bool UnmountAllPartitions(DiskInfo& disk);

  // Changes the mount path of an existing mounted filesystem.
  bool SetMountPath(std::string_view old_mount_point,
                    std::string_view new_mount_point);

  // Returns true if the disk itself or any of its partitions is currently mounted.
  bool IsDiskOrAnyPartitionMounted(const DiskInfo& disk) const;

  // Reads raw bytes from a device.
  bool ReadDeviceBytes(perception::devices::StorageDevice::Client& device,
                       uint64_t offset, size_t bytes, void* dest);

  // Writes raw bytes to a device.
  bool WriteDeviceBytes(perception::devices::StorageDevice::Client& device,
                        uint64_t offset, size_t bytes, const void* src);

 private:
  // Discovered disks list.
  std::vector<DiskInfo> disks_;

  // Update callbacks.
  std::vector<std::function<void()>> on_disks_updated_;

  // Shared memory buffer pool for device block transfers.
  perception::SharedMemoryPool<kTransferBufferSize> transfer_pool_;

  // Whether a rescan of all disks is currently in progress.
  bool is_rescanning_ = false;

  // Whether a subsequent rescan was requested while rescanning.
  bool rescan_requested_ = false;

  // Whether a disk update notification has already been deferred.
  bool update_deferred_ = false;

  // Mount event listener for filesystem changes.
  std::shared_ptr<perception::FileSystemMountListener::Server> mount_listener_;

  // Adds a newly discovered storage device.
  void OnStorageDeviceDiscovered(
      perception::devices::StorageDevice::Client device);

  // Inspects partition tables and filesystems on a disk.
  void ScanDiskInternal(DiskInfo& disk);

  // Notifies all listeners that disk state has changed.
  void NotifyDisksUpdated();
};

}  // namespace disk
}  // namespace perception
