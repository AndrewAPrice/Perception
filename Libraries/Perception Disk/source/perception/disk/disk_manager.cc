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

#include <algorithm>
#include <cstring>
#include <iostream>

#include "perception/disk/filesystem_analyzer.h"
#include "perception/disk/filesystems/exfat.h"
#include "perception/disk/formatter.h"
#include "perception/disk/gpt.h"
#include "perception/disk/mbr.h"
#include "perception/scheduler.h"
#include "perception/services.h"

namespace {

// Standard GPT entries array size in bytes (128 entries * 128 bytes = 16384
// bytes).
constexpr size_t kGptEntriesSize = 16384;

// RAII guard for acquiring and automatically releasing a pooled shared memory
// buffer.
class PooledBufferGuard {
 public:
  PooledBufferGuard(
      perception::SharedMemoryPool<perception::disk::kTransferBufferSize>& pool)
      : pool_(pool), buffer_(pool_.GetSharedMemory()) {}

  ~PooledBufferGuard() {
    if (buffer_) pool_.ReleaseSharedMemory(std::move(buffer_));
  }

  PooledBufferGuard(const PooledBufferGuard&) = delete;
  PooledBufferGuard& operator=(const PooledBufferGuard&) = delete;

  perception::PooledSharedMemory* get() const { return buffer_.get(); }
  perception::PooledSharedMemory* operator->() const { return buffer_.get(); }
  explicit operator bool() const { return buffer_ != nullptr; }

 private:
  perception::SharedMemoryPool<perception::disk::kTransferBufferSize>& pool_;
  std::shared_ptr<perception::PooledSharedMemory> buffer_;
};

class DiskManagerMountListener
    : public perception::FileSystemMountListener::Server {
 public:
  DiskManagerMountListener(perception::disk::DiskManager& manager)
      : manager_(manager) {}

  virtual Status FileSystemMounted(
      const perception::FileSystemMountEvent& event,
      perception::ProcessId sender) override {
    manager_.RescanAll();
    return Status::OK;
  }

  virtual Status FileSystemUnmounted(
      const perception::FileSystemMountEvent& event,
      perception::ProcessId sender) override {
    manager_.RescanAll();
    return Status::OK;
  }

 private:
  perception::disk::DiskManager& manager_;
};

}  // namespace

namespace perception {
namespace disk {

DiskManager::DiskManager() {}

void DiskManager::Initialize() {
  mount_listener_ = std::make_shared<DiskManagerMountListener>(*this);
  perception::NotifyOnEachNewServiceInstance<perception::StorageManager>(
      [this](perception::StorageManager::Client storage_manager) {
        storage_manager.ListenForMounts(*mount_listener_);
      });

  perception::NotifyOnEachNewServiceInstance<
      perception::devices::StorageDevice>(
      [this](perception::devices::StorageDevice::Client device) {
        OnStorageDeviceDiscovered(device);
      });
}

const std::vector<DiskInfo>& DiskManager::GetDisks() const { return disks_; }

void DiskManager::OnDisksUpdated(std::function<void()> callback) {
  on_disks_updated_.push_back(callback);
}

void DiskManager::NotifyDisksUpdated() {
  if (update_deferred_) return;
  update_deferred_ = true;
  perception::Defer([this]() {
    update_deferred_ = false;
    for (const auto& cb : on_disks_updated_) cb();
  });
}

bool DiskManager::ReadDeviceBytes(
    perception::devices::StorageDevice::Client& device, uint64_t offset,
    size_t bytes, void* dest) {
  if (!device.IsValid()) return false;

  PooledBufferGuard guard(transfer_pool_);
  if (!guard || !guard->shared_memory || !guard->shared_memory->Join())
    return false;

  size_t bytes_copied = 0;
  uint8_t* dest_bytes = static_cast<uint8_t*>(dest);

  while (bytes_copied < bytes) {
    size_t chunk = std::min(bytes - bytes_copied, kTransferBufferSize);
    perception::devices::StorageDeviceReadRequest req;
    req.offset_on_device = offset + bytes_copied;
    req.offset_in_buffer = 0;
    req.bytes_to_copy = chunk;
    req.buffer = guard->shared_memory;

    auto status = device.Read(req);
    if (status != Status::OK) return false;

    std::memcpy(dest_bytes + bytes_copied, **guard->shared_memory, chunk);
    bytes_copied += chunk;
  }
  return true;
}

bool DiskManager::WriteDeviceBytes(
    perception::devices::StorageDevice::Client& device, uint64_t offset,
    size_t bytes, const void* src) {
  if (!device.IsValid()) return false;

  PooledBufferGuard guard(transfer_pool_);
  if (!guard || !guard->shared_memory || !guard->shared_memory->Join())
    return false;

  size_t bytes_written = 0;
  const uint8_t* src_bytes = static_cast<const uint8_t*>(src);

  while (bytes_written < bytes) {
    size_t chunk = std::min(bytes - bytes_written, kTransferBufferSize);
    std::memcpy(**guard->shared_memory, src_bytes + bytes_written, chunk);

    perception::devices::StorageDeviceWriteRequest req;
    req.offset_on_device = offset + bytes_written;
    req.offset_in_buffer = 0;
    req.bytes_to_copy = chunk;
    req.buffer = guard->shared_memory;

    auto status = device.Write(req);
    if (status != Status::OK) return false;

    bytes_written += chunk;
  }
  return true;
}

void DiskManager::OnStorageDeviceDiscovered(
    perception::devices::StorageDevice::Client device) {
  auto details_or = device.GetDeviceDetails();
  if (!details_or.Ok()) return;

  DiskInfo disk;
  disk.device = device;
  disk.name = details_or->name;
  disk.size_in_bytes = details_or->size_in_bytes;
  disk.sector_size = static_cast<uint32_t>(details_or->optimal_operation_size);
  if (disk.sector_size == 0) disk.sector_size = 512;
  disk.total_sectors = disk.size_in_bytes / disk.sector_size;
  disk.is_writable = details_or->is_writable;
  disk.device_type = details_or->type;

  ScanDiskInternal(disk);

  bool found = false;
  for (auto& existing : disks_) {
    if (existing.device.ServerProcessId() == device.ServerProcessId() &&
        existing.device.ServiceId() == device.ServiceId()) {
      existing = disk;
      found = true;
      break;
    }
  }

  if (!found) {
    disks_.push_back(disk);

    device.NotifyOnDisappearance(
        [this, pid = device.ServerProcessId(), sid = device.ServiceId()]() {
          auto it = std::remove_if(disks_.begin(), disks_.end(),
                                   [pid, sid](const DiskInfo& d) {
                                     return d.device.ServerProcessId() == pid &&
                                            d.device.ServiceId() == sid;
                                   });
          if (it != disks_.end()) {
            disks_.erase(it, disks_.end());
            NotifyDisksUpdated();
          }
        });
  }

  NotifyDisksUpdated();
}

void DiskManager::RescanAll() {
  if (is_rescanning_) {
    rescan_requested_ = true;
    return;
  }
  is_rescanning_ = true;

  do {
    rescan_requested_ = false;
    for (size_t i = 0; i < disks_.size(); i++) {
      DiskInfo disk_copy = disks_[i];
      ScanDiskInternal(disk_copy);
      for (auto& d : disks_) {
        if (d.device.ServerProcessId() == disk_copy.device.ServerProcessId() &&
            d.device.ServiceId() == disk_copy.device.ServiceId()) {
          d = disk_copy;
          break;
        }
      }
    }
  } while (rescan_requested_);

  is_rescanning_ = false;
  NotifyDisksUpdated();
}

void DiskManager::RescanDisk(DiskInfo& disk) {
  DiskInfo disk_copy = disk;
  ScanDiskInternal(disk_copy);
  for (auto& d : disks_) {
    if (d.device.ServerProcessId() == disk_copy.device.ServerProcessId() &&
        d.device.ServiceId() == disk_copy.device.ServiceId()) {
      d = disk_copy;
      break;
    }
  }
  NotifyDisksUpdated();
}

void DiskManager::ScanDiskInternal(DiskInfo& disk) {
  disk.partitions.clear();
  disk.free_space_ranges.clear();
  disk.scheme = PartitionScheme::NONE;
  disk.is_mounted = false;
  disk.mount_point.clear();
  disk.is_boot_drive = false;

  if (disk.total_sectors == 0) return;

  auto reader = [this, &disk](uint64_t offset, size_t bytes,
                              void* dest) -> bool {
    return ReadDeviceBytes(disk.device, offset, bytes, dest);
  };

  std::vector<uint8_t> sector0(disk.sector_size, 0);
  std::vector<uint8_t> sector1(disk.sector_size, 0);

  bool has_sector0 = reader(0, disk.sector_size, sector0.data());
  bool has_sector1 = reader(disk.sector_size, disk.sector_size, sector1.data());

  if (has_sector0 && has_sector1 && DetectGpt(sector0.data(), sector1.data())) {
    disk.scheme = PartitionScheme::GPT;
    std::vector<uint8_t> gpt_entries(kGptEntriesSize, 0);
    uint64_t entries_offset = 2 * disk.sector_size;
    if (reader(entries_offset, kGptEntriesSize, gpt_entries.data())) {
      ParseGptPartitions(sector1.data(), gpt_entries.data(), disk.total_sectors,
                         disk.sector_size, disk.partitions,
                         disk.free_space_ranges);
    }
  } else if (has_sector0 && DetectMbr(sector0.data(), disk.total_sectors)) {
    disk.scheme = PartitionScheme::MBR;
    ParseMbrPartitions(sector0.data(), disk.total_sectors, disk.sector_size,
                       disk.partitions, disk.free_space_ranges);
  } else {
    disk.scheme = PartitionScheme::NONE;
    AnalyzeRawDevice(disk.total_sectors, disk.sector_size, reader, disk);
  }

  for (auto& part : disk.partitions)
    AnalyzeFilesystem(part.start_lba, part.sector_count, disk.sector_size,
                      reader, part);

  auto storage_manager =
      ::perception::GetService<::perception::StorageManager>();
  if (storage_manager.IsValid()) {
    auto fs_response = storage_manager.GetMountedFileSystems();
    if (fs_response.Ok()) {
      for (const auto& fs : fs_response->file_systems) {
        bool same_device =
            (fs.device.ServerProcessId() == disk.device.ServerProcessId() &&
             fs.device.ServiceId() == disk.device.ServiceId()) ||
            (!fs.device_name.empty() && fs.device_name == disk.name);
        if (!same_device) continue;

        if (fs.start_byte_offset == 0) {
          bool matched_part = false;
          for (auto& part : disk.partitions) {
            if (part.start_lba * disk.sector_size == 0) {
              part.is_mounted = true;
              part.mount_point = fs.mount_point;
              part.is_boot_drive = fs.is_boot_drive;
              if (fs.is_boot_drive) disk.is_boot_drive = true;
              matched_part = true;
              break;
            }
          }
          if (!matched_part) {
            disk.is_mounted = true;
            disk.mount_point = fs.mount_point;
            disk.is_boot_drive = fs.is_boot_drive;
          }
        } else {
          for (auto& part : disk.partitions) {
            if (part.start_lba * disk.sector_size == fs.start_byte_offset) {
              part.is_mounted = true;
              part.mount_point = fs.mount_point;
              part.is_boot_drive = fs.is_boot_drive;
              if (fs.is_boot_drive) disk.is_boot_drive = true;
              break;
            }
          }
        }
      }
    }
  }
}

bool DiskManager::InitializeDiskWithScheme(DiskInfo& disk,
                                           PartitionScheme scheme) {
  if (!disk.is_writable || disk.total_sectors == 0 ||
      IsDiskOrAnyPartitionMounted(disk))
    return false;

  auto writer = [this, &disk](uint64_t offset, size_t bytes,
                              const void* src) -> bool {
    return WriteDeviceBytes(disk.device, offset, bytes, src);
  };

  if (scheme == PartitionScheme::GPT) {
    std::vector<uint8_t> primary_header;
    std::vector<uint8_t> primary_entries;
    std::vector<uint8_t> backup_header;
    std::vector<uint8_t> backup_entries;

    CreateEmptyGptStructures(disk.total_sectors, disk.sector_size,
                             primary_header, primary_entries, backup_header,
                             backup_entries);

    auto protective_mbr = CreateProtectiveMbrSector(disk.total_sectors);
    if (!writer(0, protective_mbr.size(), protective_mbr.data())) return false;

    if (!writer(disk.sector_size, primary_header.size(), primary_header.data()))
      return false;
    if (!writer(2 * disk.sector_size, primary_entries.size(),
                primary_entries.data()))
      return false;

    size_t entries_sectors =
        (primary_entries.size() + disk.sector_size - 1) / disk.sector_size;
    uint64_t backup_entries_offset =
        (disk.total_sectors - 1 - entries_sectors) * disk.sector_size;
    uint64_t backup_header_offset = (disk.total_sectors - 1) * disk.sector_size;

    if (!writer(backup_entries_offset, backup_entries.size(),
                backup_entries.data()))
      return false;
    if (!writer(backup_header_offset, backup_header.size(),
                backup_header.data()))
      return false;

    RescanDisk(disk);
    return true;
  } else if (scheme == PartitionScheme::MBR) {
    auto mbr_sector = CreateEmptyMbrSector();
    if (!writer(0, mbr_sector.size(), mbr_sector.data())) return false;

    std::vector<uint8_t> zero_sector(disk.sector_size, 0);
    writer(disk.sector_size, zero_sector.size(), zero_sector.data());
    if (disk.total_sectors > 1)
      writer((disk.total_sectors - 1) * disk.sector_size, zero_sector.size(),
             zero_sector.data());

    RescanDisk(disk);
    return true;
  }
  return false;
}

bool DiskManager::AddPartition(DiskInfo& disk, uint64_t start_lba,
                               uint64_t sector_count, const std::string& name) {
  if (!disk.is_writable || sector_count == 0 ||
      IsDiskOrAnyPartitionMounted(disk))
    return false;

  auto reader = [this, &disk](uint64_t offset, size_t bytes,
                              void* dest) -> bool {
    return ReadDeviceBytes(disk.device, offset, bytes, dest);
  };
  auto writer = [this, &disk](uint64_t offset, size_t bytes,
                              const void* src) -> bool {
    return WriteDeviceBytes(disk.device, offset, bytes, src);
  };

  if (disk.scheme == PartitionScheme::GPT) {
    std::vector<uint8_t> primary_header_buf(disk.sector_size, 0);
    std::vector<uint8_t> entries_buf(kGptEntriesSize, 0);

    if (!reader(disk.sector_size, disk.sector_size, primary_header_buf.data()))
      return false;
    if (!reader(2 * disk.sector_size, kGptEntriesSize, entries_buf.data()))
      return false;

    GptHeader* primary_header =
        reinterpret_cast<GptHeader*>(primary_header_buf.data());

    std::vector<uint8_t> backup_header_buf(disk.sector_size, 0);
    uint64_t backup_header_offset = (disk.total_sectors - 1) * disk.sector_size;
    if (!reader(backup_header_offset, disk.sector_size,
                backup_header_buf.data()))
      return false;

    GptHeader* backup_header =
        reinterpret_cast<GptHeader*>(backup_header_buf.data());

    if (!AddGptPartitionToEntries(entries_buf, *primary_header, *backup_header,
                                  start_lba, sector_count, name))
      return false;

    if (!writer(disk.sector_size, primary_header_buf.size(),
                primary_header_buf.data()))
      return false;
    if (!writer(2 * disk.sector_size, entries_buf.size(), entries_buf.data()))
      return false;

    size_t entries_sectors =
        (entries_buf.size() + disk.sector_size - 1) / disk.sector_size;
    uint64_t backup_entries_offset =
        (disk.total_sectors - 1 - entries_sectors) * disk.sector_size;
    if (!writer(backup_entries_offset, entries_buf.size(), entries_buf.data()))
      return false;
    if (!writer(backup_header_offset, backup_header_buf.size(),
                backup_header_buf.data()))
      return false;

    RescanDisk(disk);
    return true;
  } else if (disk.scheme == PartitionScheme::MBR) {
    std::vector<uint8_t> sector0(disk.sector_size, 0);
    if (!reader(0, disk.sector_size, sector0.data())) return false;

    if (!AddMbrPartitionToSector(sector0.data(), start_lba, sector_count, 0x07))
      return false;

    if (!writer(0, sector0.size(), sector0.data())) return false;

    RescanDisk(disk);
    return true;
  }
  return false;
}

bool DiskManager::DeletePartition(DiskInfo& disk, int partition_number) {
  if (!disk.is_writable || partition_number < 1 ||
      IsDiskOrAnyPartitionMounted(disk))
    return false;

  auto reader = [this, &disk](uint64_t offset, size_t bytes,
                              void* dest) -> bool {
    return ReadDeviceBytes(disk.device, offset, bytes, dest);
  };
  auto writer = [this, &disk](uint64_t offset, size_t bytes,
                              const void* src) -> bool {
    return WriteDeviceBytes(disk.device, offset, bytes, src);
  };

  if (disk.scheme == PartitionScheme::GPT) {
    std::vector<uint8_t> primary_header_buf(disk.sector_size, 0);
    std::vector<uint8_t> entries_buf(kGptEntriesSize, 0);

    if (!reader(disk.sector_size, disk.sector_size, primary_header_buf.data()))
      return false;
    if (!reader(2 * disk.sector_size, kGptEntriesSize, entries_buf.data()))
      return false;

    GptHeader* primary_header =
        reinterpret_cast<GptHeader*>(primary_header_buf.data());

    std::vector<uint8_t> backup_header_buf(disk.sector_size, 0);
    uint64_t backup_header_offset = (disk.total_sectors - 1) * disk.sector_size;
    if (!reader(backup_header_offset, disk.sector_size,
                backup_header_buf.data()))
      return false;

    GptHeader* backup_header =
        reinterpret_cast<GptHeader*>(backup_header_buf.data());

    if (!DeleteGptPartitionFromEntries(entries_buf, *primary_header,
                                       *backup_header, partition_number - 1))
      return false;

    if (!writer(disk.sector_size, primary_header_buf.size(),
                primary_header_buf.data()))
      return false;
    if (!writer(2 * disk.sector_size, entries_buf.size(), entries_buf.data()))
      return false;

    size_t entries_sectors =
        (entries_buf.size() + disk.sector_size - 1) / disk.sector_size;
    uint64_t backup_entries_offset =
        (disk.total_sectors - 1 - entries_sectors) * disk.sector_size;
    if (!writer(backup_entries_offset, entries_buf.size(), entries_buf.data()))
      return false;
    if (!writer(backup_header_offset, backup_header_buf.size(),
                backup_header_buf.data()))
      return false;

    RescanDisk(disk);
    return true;
  } else if (disk.scheme == PartitionScheme::MBR) {
    std::vector<uint8_t> sector0(disk.sector_size, 0);
    if (!reader(0, disk.sector_size, sector0.data())) return false;

    if (!DeleteMbrPartitionFromSector(sector0.data(), partition_number - 1))
      return false;

    if (!writer(0, sector0.size(), sector0.data())) return false;

    RescanDisk(disk);
    return true;
  }
  return false;
}

bool DiskManager::ShiftDiskSectors(DiskInfo& disk, uint64_t src_lba,
                                   uint64_t dst_lba, uint64_t sector_count) {
  if (src_lba == dst_lba || sector_count == 0) return true;

  if (!disk.is_writable || IsDiskOrAnyPartitionMounted(disk)) return false;

  std::vector<uint8_t> buffer(kTransferBufferSize);
  uint64_t sectors_per_chunk = kTransferBufferSize / disk.sector_size;
  if (sectors_per_chunk == 0) sectors_per_chunk = 1;

  if (dst_lba < src_lba) {
    uint64_t sectors_copied = 0;
    while (sectors_copied < sector_count) {
      uint64_t chunk =
          std::min(sector_count - sectors_copied, sectors_per_chunk);
      uint64_t chunk_bytes = chunk * disk.sector_size;
      uint64_t read_offset = (src_lba + sectors_copied) * disk.sector_size;
      uint64_t write_offset = (dst_lba + sectors_copied) * disk.sector_size;

      if (!ReadDeviceBytes(disk.device, read_offset, chunk_bytes,
                           buffer.data()))
        return false;
      if (!WriteDeviceBytes(disk.device, write_offset, chunk_bytes,
                            buffer.data()))
        return false;

      sectors_copied += chunk;
    }
  } else {
    uint64_t sectors_remaining = sector_count;
    while (sectors_remaining > 0) {
      uint64_t chunk = std::min(sectors_remaining, sectors_per_chunk);
      uint64_t chunk_bytes = chunk * disk.sector_size;
      uint64_t offset_in_partition = sectors_remaining - chunk;
      uint64_t read_offset = (src_lba + offset_in_partition) * disk.sector_size;
      uint64_t write_offset =
          (dst_lba + offset_in_partition) * disk.sector_size;

      if (!ReadDeviceBytes(disk.device, read_offset, chunk_bytes,
                           buffer.data()))
        return false;
      if (!WriteDeviceBytes(disk.device, write_offset, chunk_bytes,
                            buffer.data()))
        return false;

      sectors_remaining -= chunk;
    }
  }

  return true;
}

bool DiskManager::PartitionAndFormatDisk(
    DiskInfo& disk, PartitionScheme scheme,
    const std::vector<PlannedPartition>& partitions) {
  if (!disk.is_writable || disk.total_sectors == 0 ||
      IsDiskOrAnyPartitionMounted(disk))
    return false;

  auto reader = [this, &disk](uint64_t offset, size_t bytes,
                              void* dest) -> bool {
    return ReadDeviceBytes(disk.device, offset, bytes, dest);
  };
  auto writer = [this, &disk](uint64_t offset, size_t bytes,
                              const void* src) -> bool {
    return WriteDeviceBytes(disk.device, offset, bytes, src);
  };

  if (scheme == PartitionScheme::NONE) {
    std::vector<uint8_t> zero_sector(disk.sector_size, 0);
    writer(disk.sector_size, zero_sector.size(), zero_sector.data());
    if (disk.total_sectors > 1)
      writer((disk.total_sectors - 1) * disk.sector_size, zero_sector.size(),
             zero_sector.data());

    std::string label = partitions.empty() ? "PERCEPTION" : partitions[0].name;
    if (label.empty()) label = "PERCEPTION";
    FilesystemType fs_type = partitions.empty() ? FilesystemType::EXFAT
                                                : partitions[0].filesystem_type;
    return FormatRawDisk(disk, label, fs_type);
  }

  uint64_t alignment_sectors = (1024 * 1024) / disk.sector_size;
  if (alignment_sectors == 0) alignment_sectors = 1;

  uint64_t current_lba = alignment_sectors;

  struct ResolvedPartition {
    PlannedPartition plan;
    uint64_t new_start_lba = 0;
    uint64_t new_sector_count = 0;
  };
  std::vector<ResolvedPartition> resolved;

  for (const auto& p : partitions) {
    if (p.size_in_bytes == 0) continue;
    uint64_t requested_sectors = p.size_in_bytes / disk.sector_size;
    if (requested_sectors == 0) continue;

    if (current_lba % alignment_sectors != 0)
      current_lba += alignment_sectors - (current_lba % alignment_sectors);

    ResolvedPartition res;
    res.plan = p;
    res.new_start_lba = current_lba;
    res.new_sector_count = requested_sectors;
    resolved.push_back(res);

    current_lba += requested_sectors;
  }

  struct ShiftTask {
    ResolvedPartition* res = nullptr;
    uint64_t current_start_lba = 0;
    uint64_t current_sector_count = 0;
    uint64_t target_start_lba = 0;
    bool completed = false;
  };
  std::vector<ShiftTask> shift_tasks;
  for (auto& res : resolved) {
    if (res.plan.is_untouched && res.plan.original_partition_number > 0) {
      if (res.new_start_lba != res.plan.original_start_lba) {
        ShiftTask task;
        task.res = &res;
        task.current_start_lba = res.plan.original_start_lba;
        task.current_sector_count = res.plan.original_sector_count;
        task.target_start_lba = res.new_start_lba;
        task.completed = false;
        shift_tasks.push_back(task);
      }
    }
  }

  size_t remaining = shift_tasks.size();
  auto execute_shift = [&](ShiftTask& task) {
    ShiftDiskSectors(disk, task.current_start_lba, task.target_start_lba,
                     task.current_sector_count);
    task.current_start_lba = task.target_start_lba;
    task.completed = true;
    remaining--;

    if (task.res->plan.filesystem_type == FilesystemType::EXFAT)
      filesystems::UpdateExfatPartitionOffset(task.target_start_lba,
                                             disk.sector_size, reader, writer);
  };

  while (remaining > 0) {
    bool shifted_any = false;
    for (auto& task : shift_tasks) {
      if (task.completed) continue;

      uint64_t target_start = task.target_start_lba;
      uint64_t target_end = target_start + task.current_sector_count;

      bool collides = false;
      for (const auto& other : shift_tasks) {
        if (&task == &other || other.completed) continue;
        uint64_t other_cur_start = other.current_start_lba;
        uint64_t other_cur_end = other_cur_start + other.current_sector_count;

        if (std::max(target_start, other_cur_start) <
            std::min(target_end, other_cur_end)) {
          collides = true;
          break;
        }
      }

      if (!collides) {
        execute_shift(task);
        shifted_any = true;
        break;
      }
    }

    if (!shifted_any) {
      for (auto& task : shift_tasks) {
        if (!task.completed) {
          execute_shift(task);
          break;
        }
      }
    }
  }

  for (auto& res : resolved) {
    if (!res.plan.is_untouched &&
        res.plan.filesystem_type != FilesystemType::RAW &&
        res.plan.filesystem_type != FilesystemType::UNKNOWN) {
      std::string label = res.plan.name.empty() ? "PERCEPTION" : res.plan.name;
      FormatFilesystem(res.plan.filesystem_type, res.new_start_lba,
                       res.new_sector_count, disk.sector_size, label, writer);
    }
  }

  if (scheme == PartitionScheme::GPT) {
    std::vector<uint8_t> primary_header_buf;
    std::vector<uint8_t> primary_entries_buf;
    std::vector<uint8_t> backup_header_buf;
    std::vector<uint8_t> backup_entries_buf;

    CreateEmptyGptStructures(disk.total_sectors, disk.sector_size,
                             primary_header_buf, primary_entries_buf,
                             backup_header_buf, backup_entries_buf);

    GptHeader* primary_header =
        reinterpret_cast<GptHeader*>(primary_header_buf.data());
    GptHeader* backup_header =
        reinterpret_cast<GptHeader*>(backup_header_buf.data());

    for (const auto& res : resolved) {
      const uint8_t* guid = nullptr;
      if (res.plan.name == "BIOS Boot" ||
          res.plan.name == "BIOS Boot Partition")
        guid = GetBiosBootGuid();
      AddGptPartitionToEntries(primary_entries_buf, *primary_header,
                               *backup_header, res.new_start_lba,
                               res.new_sector_count, res.plan.name, guid);
    }

    auto pmbr = CreateProtectiveMbrSector(disk.total_sectors);
    if (!writer(0, pmbr.size(), pmbr.data())) return false;

    if (!writer(disk.sector_size, primary_header_buf.size(),
                primary_header_buf.data()))
      return false;
    if (!writer(2 * disk.sector_size, primary_entries_buf.size(),
                primary_entries_buf.data()))
      return false;

    size_t entries_sectors =
        (primary_entries_buf.size() + disk.sector_size - 1) / disk.sector_size;
    uint64_t backup_entries_offset =
        (disk.total_sectors - 1 - entries_sectors) * disk.sector_size;
    uint64_t backup_header_offset = (disk.total_sectors - 1) * disk.sector_size;

    if (!writer(backup_entries_offset, primary_entries_buf.size(),
                primary_entries_buf.data()))
      return false;
    if (!writer(backup_header_offset, backup_header_buf.size(),
                backup_header_buf.data()))
      return false;

  } else if (scheme == PartitionScheme::MBR) {
    auto sector0 = CreateEmptyMbrSector();
    for (size_t i = 0; i < resolved.size() && i < 4; i++) {
      AddMbrPartitionToSector(sector0.data(), resolved[i].new_start_lba,
                              resolved[i].new_sector_count, 0x07);
    }
    if (!writer(0, sector0.size(), sector0.data())) return false;

    std::vector<uint8_t> zero_sector(disk.sector_size, 0);
    writer(disk.sector_size, zero_sector.size(), zero_sector.data());
    if (disk.total_sectors > 1)
      writer((disk.total_sectors - 1) * disk.sector_size, zero_sector.size(),
             zero_sector.data());
  }

  RescanDisk(disk);
  return true;
}

bool DiskManager::FormatPartition(DiskInfo& disk, int partition_number,
                                  const std::string& volume_label,
                                  FilesystemType filesystem) {
  if (!disk.is_writable) return false;

  auto part_itr = std::find_if(disk.partitions.begin(), disk.partitions.end(),
                               [partition_number](const PartitionInfo& p) {
                                 return p.partition_number == partition_number;
                               });

  if (part_itr == disk.partitions.end() || part_itr->is_mounted) return false;

  auto writer = [this, &disk](uint64_t offset, size_t bytes,
                              const void* src) -> bool {
    return WriteDeviceBytes(disk.device, offset, bytes, src);
  };

  if (!FormatFilesystem(filesystem, part_itr->start_lba, part_itr->sector_count,
                        disk.sector_size, volume_label, writer))
    return false;

  RescanDisk(disk);
  return true;
}

bool DiskManager::FormatPartition(DiskInfo& disk, int partition_number,
                                  const std::string& volume_label) {
  return FormatPartition(disk, partition_number, volume_label,
                         FilesystemType::EXFAT);
}

bool DiskManager::FormatRawDisk(DiskInfo& disk, const std::string& volume_label,
                                FilesystemType filesystem) {
  if (!disk.is_writable || disk.total_sectors == 0 || disk.is_mounted)
    return false;

  auto writer = [this, &disk](uint64_t offset, size_t bytes,
                              const void* src) -> bool {
    return WriteDeviceBytes(disk.device, offset, bytes, src);
  };

  if (!FormatFilesystem(filesystem, 0, disk.total_sectors, disk.sector_size,
                        volume_label, writer))
    return false;

  RescanDisk(disk);
  return true;
}

bool DiskManager::FormatRawDisk(DiskInfo& disk,
                                const std::string& volume_label) {
  return FormatRawDisk(disk, volume_label, FilesystemType::EXFAT);
}

bool DiskManager::MountDisk(DiskInfo& disk,
                            std::string_view target_mount_point) {
  if (disk.scheme != PartitionScheme::NONE || !disk.partitions.empty())
    return MountAllPartitions(disk);

  auto storage_manager =
      ::perception::GetService<::perception::StorageManager>();
  if (!storage_manager.IsValid()) return false;

  perception::MountFileSystemRequest req;
  req.device = disk.device;
  req.start_byte_offset = 0;
  req.byte_length = disk.size_in_bytes;
  req.target_mount_point = std::string(target_mount_point);

  auto res = storage_manager.MountFileSystem(req);
  if (!res.Ok()) return false;

  RescanDisk(disk);
  return true;
}

bool DiskManager::MountAllPartitions(DiskInfo& disk) {
  auto storage_manager =
      ::perception::GetService<::perception::StorageManager>();
  if (!storage_manager.IsValid()) return false;

  bool any_mounted = false;
  for (const auto& part : disk.partitions) {
    if (part.is_mounted) continue;

    perception::MountFileSystemRequest req;
    req.device = disk.device;
    req.start_byte_offset = part.start_lba * disk.sector_size;
    req.byte_length = part.sector_count * disk.sector_size;
    req.target_mount_point = "";

    auto res = storage_manager.MountFileSystem(req);
    if (res.Ok()) any_mounted = true;
  }

  RescanDisk(disk);
  return any_mounted;
}

bool DiskManager::MountPartition(DiskInfo& disk, int partition_number,
                                 std::string_view target_mount_point) {
  auto storage_manager =
      ::perception::GetService<::perception::StorageManager>();
  if (!storage_manager.IsValid()) return false;

  for (const auto& part : disk.partitions) {
    if (part.partition_number == partition_number) {
      perception::MountFileSystemRequest req;
      req.device = disk.device;
      req.start_byte_offset = part.start_lba * disk.sector_size;
      req.byte_length = part.sector_count * disk.sector_size;
      req.target_mount_point = std::string(target_mount_point);

      auto res = storage_manager.MountFileSystem(req);
      if (!res.Ok()) return false;

      RescanDisk(disk);
      return true;
    }
  }
  return false;
}

bool DiskManager::Unmount(std::string_view mount_point) {
  auto storage_manager =
      ::perception::GetService<::perception::StorageManager>();
  if (!storage_manager.IsValid()) return false;

  perception::RequestWithFilePath req;
  req.path = std::string(mount_point);
  auto status = storage_manager.UnmountFileSystem(req);
  if (status != Status::OK) return false;

  RescanAll();
  return true;
}

bool DiskManager::UnmountAllPartitions(DiskInfo& disk) {
  auto storage_manager =
      ::perception::GetService<::perception::StorageManager>();
  if (!storage_manager.IsValid()) return false;

  bool any_unmounted = false;
  for (const auto& part : disk.partitions) {
    if (!part.is_mounted || part.is_boot_drive) continue;

    perception::RequestWithFilePath req;
    req.path = part.mount_point;
    auto status = storage_manager.UnmountFileSystem(req);
    if (status == Status::OK) any_unmounted = true;
  }

  RescanDisk(disk);
  return any_unmounted;
}

bool DiskManager::SetMountPath(std::string_view old_mount_point,
                               std::string_view new_mount_point) {
  auto storage_manager =
      ::perception::GetService<::perception::StorageManager>();
  if (!storage_manager.IsValid()) return false;

  perception::SetMountPathRequest req;
  req.old_mount_point = std::string(old_mount_point);
  req.new_mount_point = std::string(new_mount_point);
  auto status = storage_manager.SetMountPath(req);
  if (status != Status::OK) return false;

  RescanAll();
  return true;
}

bool DiskManager::IsDiskOrAnyPartitionMounted(const DiskInfo& disk) const {
  if (disk.is_mounted) return true;
  for (const auto& part : disk.partitions) {
    if (part.is_mounted) return true;
  }
  return false;
}

}  // namespace disk
}  // namespace perception
