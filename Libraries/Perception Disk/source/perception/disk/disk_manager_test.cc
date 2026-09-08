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

#include "perception/disk/disk_manager.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "perception/disk/disk_structures.h"
#include "perception/shared_memory_pool.h"
#include "testing.h"

namespace {

using ::perception::disk::DiskInfo;
using ::perception::disk::DiskManager;
using ::perception::disk::FilesystemType;
using ::perception::disk::kTransferBufferSize;
using ::perception::disk::PartitionInfo;
using ::perception::disk::PartitionScheme;
using ::perception::disk::PlannedPartition;

TEST(DiskManagerSharedMemoryPoolIsolation) {
  perception::SharedMemoryPool<kTransferBufferSize> pool;

  auto buffer1 = pool.GetSharedMemory();
  EXPECT(true, buffer1 != nullptr);
  EXPECT(true, buffer1->shared_memory != nullptr);

  auto buffer2 = pool.GetSharedMemory();
  EXPECT(true, buffer2 != nullptr);
  EXPECT(true, buffer2->shared_memory != nullptr);

  // Two concurrently acquired buffers from the pool must be distinct.
  EXPECT(false,
         buffer1->shared_memory->GetId() == buffer2->shared_memory->GetId());

  // Writing to buffer1 must not affect buffer2.
  uint8_t* ptr1 = static_cast<uint8_t*>(**buffer1->shared_memory);
  uint8_t* ptr2 = static_cast<uint8_t*>(**buffer2->shared_memory);
  ptr1[0] = 0xAA;
  ptr2[0] = 0xBB;
  EXPECT((uint8_t)0xAA, ptr1[0]);
  EXPECT((uint8_t)0xBB, ptr2[0]);

  // Buffers can be released and reacquired.
  pool.ReleaseSharedMemory(std::move(buffer1));
  pool.ReleaseSharedMemory(std::move(buffer2));

  auto buffer3 = pool.GetSharedMemory();
  EXPECT(true, buffer3 != nullptr);
  pool.ReleaseSharedMemory(std::move(buffer3));
}

TEST(DiskManagerIsDiskOrAnyPartitionMounted) {
  DiskManager disk_manager;
  DiskInfo disk;

  // Unmounted disk and empty partitions.
  disk.is_mounted = false;
  EXPECT(false, disk_manager.IsDiskOrAnyPartitionMounted(disk));

  // Disk itself is mounted.
  disk.is_mounted = true;
  EXPECT(true, disk_manager.IsDiskOrAnyPartitionMounted(disk));

  // Disk unmounted, but has unmounted partition.
  disk.is_mounted = false;
  PartitionInfo part1;
  part1.partition_number = 1;
  part1.is_mounted = false;
  disk.partitions.push_back(part1);
  EXPECT(false, disk_manager.IsDiskOrAnyPartitionMounted(disk));

  // Disk unmounted, but has mounted partition.
  PartitionInfo part2;
  part2.partition_number = 2;
  part2.is_mounted = true;
  disk.partitions.push_back(part2);
  EXPECT(true, disk_manager.IsDiskOrAnyPartitionMounted(disk));
}

TEST(DiskManagerValidationGuards) {
  DiskManager disk_manager;
  DiskInfo disk;
  disk.is_writable = false;
  disk.total_sectors = 0;

  // Read-only or zero-sector disk rejected for initialization.
  EXPECT(false,
         disk_manager.InitializeDiskWithScheme(disk, PartitionScheme::GPT));
  EXPECT(false,
         disk_manager.InitializeDiskWithScheme(disk, PartitionScheme::MBR));

  // Read-only or zero-sector disk rejected for formatting.
  EXPECT(false, disk_manager.FormatRawDisk(disk, "LABEL"));
  EXPECT(false,
         disk_manager.FormatRawDisk(disk, "LABEL", FilesystemType::EXFAT));

  disk.is_writable = true;
  disk.total_sectors = 2097152;
  disk.scheme = PartitionScheme::NONE;

  // AddPartition rejected if sector_count is 0 or scheme is NONE.
  EXPECT(false, disk_manager.AddPartition(disk, 2048, 0, "Part"));
  EXPECT(false, disk_manager.AddPartition(disk, 2048, 1000, "Part"));

  // DeletePartition rejected if partition_number < 1.
  EXPECT(false, disk_manager.DeletePartition(disk, 0));
  EXPECT(false, disk_manager.DeletePartition(disk, -1));

  // ShiftDiskSectors returns true if src == dst or count == 0.
  EXPECT(true, disk_manager.ShiftDiskSectors(disk, 2048, 2048, 5000));
  EXPECT(true, disk_manager.ShiftDiskSectors(disk, 2048, 4096, 0));

  // ShiftDiskSectors rejected if read-only or mounted.
  disk.is_writable = false;
  EXPECT(false, disk_manager.ShiftDiskSectors(disk, 2048, 4096, 100));

  disk.is_writable = true;
  disk.is_mounted = true;
  EXPECT(false, disk_manager.ShiftDiskSectors(disk, 2048, 4096, 100));
  EXPECT(false,
         disk_manager.InitializeDiskWithScheme(disk, PartitionScheme::GPT));
  EXPECT(false, disk_manager.FormatRawDisk(disk, "LABEL"));

  // FormatPartition rejected if partition not found or mounted.
  disk.is_mounted = false;
  EXPECT(false, disk_manager.FormatPartition(disk, 99, "LABEL"));

  PartitionInfo mounted_part;
  mounted_part.partition_number = 1;
  mounted_part.is_mounted = true;
  disk.partitions.push_back(mounted_part);
  EXPECT(false, disk_manager.FormatPartition(disk, 1, "LABEL"));

  // PartitionAndFormatDisk rejected if read-only, total_sectors is 0, or mounted.
  std::vector<PlannedPartition> dummy_plan;
  disk.is_writable = false;
  EXPECT(false, disk_manager.PartitionAndFormatDisk(disk, PartitionScheme::GPT,
                                                   dummy_plan));

  disk.is_writable = true;
  disk.total_sectors = 0;
  EXPECT(false, disk_manager.PartitionAndFormatDisk(disk, PartitionScheme::GPT,
                                                   dummy_plan));

  disk.total_sectors = 2097152;
  disk.is_mounted = true;
  EXPECT(false, disk_manager.PartitionAndFormatDisk(disk, PartitionScheme::GPT,
                                                   dummy_plan));
}


TEST(DiskManagerMountUnmountSafeFailures) {
  DiskManager disk_manager;
  DiskInfo disk;
  disk.scheme = PartitionScheme::NONE;

  // Without active StorageManager service or valid device, operations fail
  // gracefully without throwing or crashing.
  EXPECT(false, disk_manager.MountDisk(disk));
  EXPECT(false, disk_manager.MountAllPartitions(disk));
  EXPECT(false, disk_manager.MountPartition(disk, 1));
  EXPECT(false, disk_manager.Unmount("/Drive 1/"));
  EXPECT(false, disk_manager.UnmountAllPartitions(disk));
  EXPECT(false, disk_manager.SetMountPath("/Drive 1/", "/Drive 2/"));

  // Initial state checks.
  EXPECT((size_t)0, disk_manager.GetDisks().size());

  bool callback_registered = false;
  disk_manager.OnDisksUpdated([&callback_registered]() {
    callback_registered = true;
  });
  EXPECT(false, callback_registered);
}

}  // namespace
