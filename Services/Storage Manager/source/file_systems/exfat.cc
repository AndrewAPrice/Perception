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

#include "file_systems/exfat.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "perception/scheduler.h"
#include "perception/storage_manager.h"
#include "shared_memory_pool.h"
#include "virtual_file_system.h"

using ::perception::Defer;
using ::perception::DirectoryEntry;
using ::perception::FileStatistics;
using ::perception::
    GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest;
using ::perception::ProcessId;
using ::perception::ReadFileRequest;
using ::perception::StorageManager;
using ::perception::WriteFileRequest;
using ::perception::devices::StorageDevice;
using ::perception::devices::StorageDeviceReadRequest;
using ::perception::devices::StorageDeviceWriteRequest;

namespace file_systems {
namespace {

// Standard exFAT file system name.
constexpr std::string_view kExfatName = "exFAT";

// Entry type byte identifying allocation bitmap.
constexpr uint8 kEntryTypeAllocationBitmap = 0x81;

// Entry type byte identifying up-case table.
constexpr uint8 kEntryTypeUpcaseTable = 0x82;

// Entry type byte identifying volume label.
constexpr uint8 kEntryTypeVolumeLabel = 0x83;

// Entry type byte identifying file directory entry.
constexpr uint8 kEntryTypeFile = 0x85;

// Entry type byte identifying stream extension directory entry.
constexpr uint8 kEntryTypeStreamExtension = 0xC0;

// Entry type byte identifying file name directory entry.
constexpr uint8 kEntryTypeFileName = 0xC1;

// End of cluster chain marker in FAT.
constexpr uint32 kFatEndOfChain = 0xFFFFFFFF;

// Mask indicating directory attribute.
constexpr uint16 kAttributeDirectory = 0x0010;

// Flag indicating stream has no FAT chain (contiguous).
constexpr uint8 kStreamFlagNoFatChain = 0x02;

// Flag indicating stream allocation is possible.
constexpr uint8 kStreamFlagAllocationPossible = 0x01;

// Rotates a 16-bit value right by 1 bit and adds an 8-bit byte.
constexpr uint16 Ror1Add(uint16 acc, uint8 val) {
  return ((acc << 15) | (acc >> 1)) + val;
}

// exFAT Boot Sector.
struct __attribute__((packed)) ExfatBootSector {
  uint8 jump_boot[3];
  char fs_name[8];
  uint8 must_be_zero[53];
  uint64 partition_offset;
  uint64 volume_length;
  uint32 fat_offset;
  uint32 fat_length;
  uint32 cluster_heap_offset;
  uint32 cluster_count;
  uint32 root_dir_first_cluster;
  uint32 volume_serial_number;
  uint16 fs_revision;
  uint16 volume_flags;
  uint8 bytes_per_sector_shift;
  uint8 sectors_per_cluster_shift;
  uint8 number_of_fats;
  uint8 drive_select;
  uint8 percent_in_use;
  uint8 reserved[7];
  uint8 boot_code[390];
  uint16 boot_signature;
};
static_assert(sizeof(ExfatBootSector) == 512, "ExfatBootSector must be 512 bytes");

// Generic 32-byte directory entry.
struct __attribute__((packed)) ExfatDirectoryEntryHeader {
  uint8 entry_type;
  uint8 custom_defined[31];
};
static_assert(sizeof(ExfatDirectoryEntryHeader) == 32, "Entry header must be 32 bytes");

// File Directory Entry (0x85).
struct __attribute__((packed)) ExfatFileDirectoryEntry {
  uint8 entry_type;
  uint8 secondary_count;
  uint16 set_checksum;
  uint16 file_attributes;
  uint16 reserved1;
  uint32 create_timestamp;
  uint32 last_modified_timestamp;
  uint32 last_accessed_timestamp;
  uint8 create_10ms_increment;
  uint8 last_modified_10ms_increment;
  uint8 create_utc_offset;
  uint8 last_modified_utc_offset;
  uint8 last_accessed_utc_offset;
  uint8 reserved2[7];
};
static_assert(sizeof(ExfatFileDirectoryEntry) == 32, "ExfatFileDirectoryEntry must be 32 bytes");

// Stream Extension Directory Entry (0xC0).
struct __attribute__((packed)) ExfatStreamExtensionEntry {
  uint8 entry_type;
  uint8 general_flags;
  uint8 reserved1;
  uint8 name_length;
  uint16 name_hash;
  uint16 reserved2;
  uint64 valid_data_length;
  uint32 reserved3;
  uint32 first_cluster;
  uint64 data_length;
};
static_assert(sizeof(ExfatStreamExtensionEntry) == 32, "ExfatStreamExtensionEntry must be 32 bytes");

// File Name Directory Entry (0xC1).
struct __attribute__((packed)) ExfatFileNameEntry {
  uint8 entry_type;
  uint8 general_flags;
  char16_t file_name[15];
};
static_assert(sizeof(ExfatFileNameEntry) == 32, "ExfatFileNameEntry must be 32 bytes");

// Allocation Bitmap Directory Entry (0x81).
struct __attribute__((packed)) ExfatAllocationBitmapEntry {
  uint8 entry_type;
  uint8 bitmap_flags;
  uint8 reserved[18];
  uint32 first_cluster;
  uint64 data_length;
};
static_assert(sizeof(ExfatAllocationBitmapEntry) == 32, "ExfatAllocationBitmapEntry must be 32 bytes");

// Computes 16-bit set checksum across directory entry set.
uint16 ComputeSetChecksum(const uint8* entry_set, size_t entry_count) {
  uint16 checksum = 0;
  size_t total_bytes = entry_count * sizeof(ExfatDirectoryEntryHeader);
  for (size_t i = 0; i < total_bytes; i++) {
    if (i == 2 || i == 3) continue;
    checksum = Ror1Add(checksum, entry_set[i]);
  }
  return checksum;
}

// Finalizes set checksum in a directory entry set.
void FinalizeEntrySetChecksum(uint8* entry_set, size_t entry_count) {
  auto* file_entry = reinterpret_cast<ExfatFileDirectoryEntry*>(entry_set);
  file_entry->set_checksum = ComputeSetChecksum(entry_set, entry_count);
}

// Computes 16-bit name hash of upcased UTF-16 string.
uint16 ComputeNameHash(const std::u16string& upcased_name) {
  uint16 hash = 0;
  for (char16_t ch : upcased_name) {
    hash = Ror1Add(hash, static_cast<uint8>(ch & 0xFF));
    hash = Ror1Add(hash, static_cast<uint8>((ch >> 8) & 0xFF));
  }
  return hash;
}

// Converts UTF-8 string to UTF-16.
std::u16string Utf8ToUtf16(std::string_view utf8) {
  std::u16string utf16;
  size_t i = 0;
  while (i < utf8.size()) {
    uint32 codepoint = 0;
    uint8 b0 = static_cast<uint8>(utf8[i]);
    if (b0 < 0x80) {
      codepoint = b0;
      i += 1;
    } else if ((b0 & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
      codepoint = ((b0 & 0x1F) << 6) | (static_cast<uint8>(utf8[i + 1]) & 0x3F);
      i += 2;
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
      codepoint = ((b0 & 0x0F) << 12) |
                  ((static_cast<uint8>(utf8[i + 1]) & 0x3F) << 6) |
                  (static_cast<uint8>(utf8[i + 2]) & 0x3F);
      i += 3;
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
      codepoint = ((b0 & 0x07) << 18) |
                  ((static_cast<uint8>(utf8[i + 1]) & 0x3F) << 12) |
                  ((static_cast<uint8>(utf8[i + 2]) & 0x3F) << 6) |
                  (static_cast<uint8>(utf8[i + 3]) & 0x3F);
      i += 4;
    } else {
      codepoint = '?';
      i += 1;
    }

    if (codepoint <= 0xFFFF) {
      utf16.push_back(static_cast<char16_t>(codepoint));
    } else {
      codepoint -= 0x10000;
      utf16.push_back(static_cast<char16_t>((codepoint >> 10) + 0xD800));
      utf16.push_back(static_cast<char16_t>((codepoint & 0x3FF) + 0xDC00));
    }
  }
  return utf16;
}

// Converts UTF-16 string to UTF-8.
std::string Utf16ToUtf8(const char16_t* utf16, size_t length) {
  std::string utf8;
  for (size_t i = 0; i < length; i++) {
    uint32 codepoint = utf16[i];
    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 1 < length) {
      uint32 low = utf16[i + 1];
      if (low >= 0xDC00 && low <= 0xDFFF) {
        codepoint = ((codepoint - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
        i++;
      }
    }

    if (codepoint < 0x80) {
      utf8.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
      utf8.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
      utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
      utf8.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
      utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
      utf8.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
      utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
  }
  return utf8;
}

class ExfatFile : public File {
 public:
  ExfatFile(ExfatFileSystem& fs, uint32 dir_cluster, uint64 dir_entry_offset,
            size_t secondary_count, uint32 first_cluster, uint64 data_length,
            bool no_fat_chain, ProcessId allowed_process)
      : fs_(fs),
        dir_cluster_(dir_cluster),
        dir_entry_offset_(dir_entry_offset),
        secondary_count_(secondary_count),
        first_cluster_(first_cluster),
        data_length_(data_length),
        no_fat_chain_(no_fat_chain),
        allowed_process_(allowed_process) {}

  virtual ~ExfatFile() override {}

  virtual Status Close(ProcessId sender) override {
    if (allowed_process_ != 0 && sender != allowed_process_)
      return Status::NOT_ALLOWED;
    if (sender != 0) Defer([sender, this]() { CloseFile(sender, this); });
    return Status::OK;
  }

  virtual Status Read(const ReadFileRequest& request,
                      ProcessId sender) override {
    if (sender != allowed_process_) return Status::NOT_ALLOWED;
    if (request.offset_in_file + request.bytes_to_copy > data_length_)
      return Status::OVERFLOW;
    if (request.bytes_to_copy == 0) return Status::OK;

    if (!request.buffer_to_copy_into->Join()) return Status::INVALID_ARGUMENT;
    uint8* dest = static_cast<uint8*>(**request.buffer_to_copy_into) +
                  request.offset_in_destination_buffer;

    return fs_.ReadClusters(first_cluster_, no_fat_chain_,
                            request.offset_in_file, request.bytes_to_copy,
                            dest);
  }

  virtual Status Write(const WriteFileRequest& request,
                       ProcessId sender) override {
    if (sender != allowed_process_) return Status::NOT_ALLOWED;
    if (!fs_.IsWritable()) return Status::NOT_ALLOWED;

    if (!request.buffer_to_copy_from->Join()) return Status::INVALID_ARGUMENT;
    const uint8* src = static_cast<const uint8*>(**request.buffer_to_copy_from);

    uint64 end_offset = request.offset_in_file + request.bytes_to_copy;
    uint32 cluster_size = fs_.GetClusterSize();

    if (end_offset > data_length_) {
      Status extend_status = fs_.ExtendClusters(first_cluster_, no_fat_chain_,
                                                data_length_, end_offset);
      if (extend_status != Status::OK) return extend_status;

      data_length_ = end_offset;
      Status status =
          fs_.UpdateFileEntry(dir_cluster_, dir_entry_offset_, secondary_count_,
                              first_cluster_, data_length_, no_fat_chain_);
      if (status != Status::OK) return status;
    }

    if (request.bytes_to_copy == 0) return Status::OK;

    return fs_.WriteClusters(first_cluster_, no_fat_chain_,
                             request.offset_in_file, request.bytes_to_copy,
                             src);
  }

  virtual Status GrantStorageDevicePermissionToAllocateSharedMemoryPages(
      const GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest&
          request,
      ::perception::ProcessId sender) override {
    if (sender != allowed_process_) return Status::NOT_ALLOWED;
    request.buffer->GrantPermissionToLazilyAllocatePage(
        fs_.GetStorageDevice().ServerProcessId());
    return Status::OK;
  }

 private:
  ExfatFileSystem& fs_;
  uint32 dir_cluster_;
  uint64 dir_entry_offset_;
  size_t secondary_count_;
  uint32 first_cluster_;
  uint64 data_length_;
  bool no_fat_chain_;
  ProcessId allowed_process_;
};

}  // namespace

ExfatFileSystem::ExfatFileSystem(
    StorageDevice::Client storage_device, uint64 volume_length,
    uint32 fat_offset, uint32 fat_length, uint32 cluster_heap_offset,
    uint32 cluster_count, uint32 first_cluster_of_root_dir,
    uint8 bytes_per_sector_shift, uint8 sectors_per_cluster_shift,
    uint64 start_byte_offset)
    : FileSystem(storage_device),
      volume_length_(volume_length),
      fat_offset_(fat_offset),
      fat_length_(fat_length),
      cluster_heap_offset_(cluster_heap_offset),
      cluster_count_(cluster_count),
      first_cluster_of_root_dir_(first_cluster_of_root_dir),
      bytes_per_sector_shift_(bytes_per_sector_shift),
      sectors_per_cluster_shift_(sectors_per_cluster_shift),
      bitmap_first_cluster_(0),
      bitmap_data_length_(0) {
  start_byte_offset_ = start_byte_offset;
  sector_size_ = 1 << bytes_per_sector_shift_;
  byte_length_ = volume_length_ * sector_size_;
  cluster_size_ = 1 << (bytes_per_sector_shift_ + sectors_per_cluster_shift_);
  sectors_per_cluster_ = 1 << sectors_per_cluster_shift_;
  optimal_operation_size_ = cluster_size_;

  LoadAllocationBitmap();
}

ExfatFileSystem::~ExfatFileSystem() {}

uint64 ExfatFileSystem::ClusterToDeviceOffset(uint32 cluster) const {
  return start_byte_offset_ +
         (static_cast<uint64>(cluster_heap_offset_) +
          static_cast<uint64>(cluster - 2) * sectors_per_cluster_) *
             sector_size_;
}

uint64 ExfatFileSystem::FatEntryToDeviceOffset(uint32 cluster) const {
  return start_byte_offset_ +
         (static_cast<uint64>(fat_offset_) * sector_size_) +
         (static_cast<uint64>(cluster) * 4);
}

StatusOr<uint32> ExfatFileSystem::GetNextCluster(uint32 cluster) {
  if (cluster < 2 || cluster >= cluster_count_ + 2)
    return uint32(kFatEndOfChain);

  uint64 offset = FatEntryToDeviceOffset(cluster);
  auto pooled = kSharedMemoryPool.GetSharedMemory();
  StorageDeviceReadRequest req;
  req.offset_on_device = offset;
  req.offset_in_buffer = 0;
  req.bytes_to_copy = 4;
  req.buffer = pooled->shared_memory;

  Status status = storage_device_.Read(req);
  if (status != Status::OK) {
    kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
    return status;
  }

  uint32 next_cluster = *reinterpret_cast<uint32*>(**pooled->shared_memory);
  kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
  return next_cluster;
}

Status ExfatFileSystem::SetNextCluster(uint32 cluster, uint32 next_cluster) {
  if (cluster < 2 || cluster >= cluster_count_ + 2)
    return Status::INVALID_ARGUMENT;

  uint64 offset = FatEntryToDeviceOffset(cluster);
  auto pooled = kSharedMemoryPool.GetSharedMemory();
  *reinterpret_cast<uint32*>(**pooled->shared_memory) = next_cluster;

  StorageDeviceWriteRequest req;
  req.offset_on_device = offset;
  req.offset_in_buffer = 0;
  req.bytes_to_copy = 4;
  req.buffer = pooled->shared_memory;

  Status status = storage_device_.Write(req);
  kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
  return status;
}

StatusOr<uint32> ExfatFileSystem::FindLastCluster(uint32 first_cluster) {
  uint32 cur = first_cluster;
  while (true) {
    auto next_or = GetNextCluster(cur);
    if (!next_or.Ok() || IsEndOfChain(*next_or)) break;
    cur = *next_or;
  }
  return cur;
}

StatusOr<uint32> ExfatFileSystem::AllocateZeroedCluster() {
  std::vector<uint32> allocated;
  auto status_or_cluster = AllocateClusters(1, false, allocated);
  if (!status_or_cluster.Ok()) return status_or_cluster.Status();
  uint32 new_cluster = *status_or_cluster;

  std::vector<uint8> zero_cluster(cluster_size_, 0);
  Status write_status =
      WriteClusters(new_cluster, true, 0, cluster_size_, zero_cluster.data());
  if (write_status != Status::OK) return write_status;
  return new_cluster;
}

bool ExfatFileSystem::IsClusterAllocated(uint32 cluster) const {
  size_t bit_idx = cluster - 2;
  size_t byte_idx = bit_idx / 8;
  if (byte_idx >= allocation_bitmap_.size()) return true;
  return (allocation_bitmap_[byte_idx] & (1 << (bit_idx % 8))) != 0;
}

void ExfatFileSystem::SetClusterAllocated(uint32 cluster, bool allocated) {
  size_t bit_idx = cluster - 2;
  size_t byte_idx = bit_idx / 8;
  if (byte_idx >= allocation_bitmap_.size()) return;
  if (allocated) {
    allocation_bitmap_[byte_idx] |= (1 << (bit_idx % 8));
  } else {
    allocation_bitmap_[byte_idx] &= ~(1 << (bit_idx % 8));
  }
}

void ExfatFileSystem::SetClusterRangeAllocated(uint32 start_cluster,
                                              uint32 count, bool allocated) {
  for (uint32 k = 0; k < count; k++)
    SetClusterAllocated(start_cluster + k, allocated);
}

Status ExfatFileSystem::FlushClusterBitmapRange(uint32 start_cluster,
                                               uint32 count) {
  if (count == 0) return Status::OK;
  size_t start_b = (start_cluster - 2) / 8;
  size_t end_b = (start_cluster + count - 2) / 8;
  return FlushAllocationBitmap(start_b, end_b - start_b + 1);
}

Status ExfatFileSystem::LoadAllocationBitmap() {
  ForEachRawEntryInDirectory(
      first_cluster_of_root_dir_,
      [this](const ExfatEntryInfo& entry) { return false; });

  if (bitmap_first_cluster_ < 2) return Status::INTERNAL_ERROR;

  allocation_bitmap_.resize(bitmap_data_length_);
  return ReadClusters(bitmap_first_cluster_, false, 0, bitmap_data_length_,
                      allocation_bitmap_.data());
}

Status ExfatFileSystem::FlushAllocationBitmap(size_t byte_start,
                                              size_t byte_length) {
  if (bitmap_first_cluster_ < 2) return Status::INTERNAL_ERROR;
  return WriteClusters(bitmap_first_cluster_, false, byte_start, byte_length,
                       allocation_bitmap_.data() + byte_start);
}

Status ExfatFileSystem::TransferClusters(uint32 first_cluster, bool no_fat_chain,
                                         uint64 offset_in_stream,
                                         uint64 bytes_to_copy, uint8* buffer,
                                         bool is_write) {
  if (bytes_to_copy == 0) return Status::OK;

  uint32 cur_cluster = first_cluster;
  uint64 cluster_idx = offset_in_stream / cluster_size_;
  uint64 offset_in_cluster = offset_in_stream % cluster_size_;

  if (no_fat_chain) {
    cur_cluster += static_cast<uint32>(cluster_idx);
  } else {
    for (uint64 i = 0; i < cluster_idx; i++) {
      auto status_or_next = GetNextCluster(cur_cluster);
      if (!status_or_next.Ok()) return status_or_next.Status();
      cur_cluster = *status_or_next;
      if (IsEndOfChain(cur_cluster)) return Status::OVERFLOW;
    }
  }

  uint64 bytes_remaining = bytes_to_copy;
  uint64 buffer_offset = 0;

  auto pooled = kSharedMemoryPool.GetSharedMemory();

  while (bytes_remaining > 0) {
    if (!IsValidCluster(cur_cluster)) {
      kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
      return Status::OVERFLOW;
    }

    uint64 chunk_available = cluster_size_ - offset_in_cluster;
    uint64 chunk_to_copy = std::min(bytes_remaining, chunk_available);
    uint64 dev_offset = ClusterToDeviceOffset(cur_cluster) + offset_in_cluster;

    if (is_write) {
      std::memcpy(**pooled->shared_memory, buffer + buffer_offset,
                  chunk_to_copy);
      StorageDeviceWriteRequest req;
      req.offset_on_device = dev_offset;
      req.offset_in_buffer = 0;
      req.bytes_to_copy = chunk_to_copy;
      req.buffer = pooled->shared_memory;
      Status status = storage_device_.Write(req);
      if (status != Status::OK) {
        kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
        return status;
      }
    } else {
      StorageDeviceReadRequest req;
      req.offset_on_device = dev_offset;
      req.offset_in_buffer = 0;
      req.bytes_to_copy = chunk_to_copy;
      req.buffer = pooled->shared_memory;
      Status status = storage_device_.Read(req);
      if (status != Status::OK) {
        kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
        return status;
      }
      std::memcpy(buffer + buffer_offset, **pooled->shared_memory,
                  chunk_to_copy);
    }

    buffer_offset += chunk_to_copy;
    bytes_remaining -= chunk_to_copy;
    offset_in_cluster = 0;

    if (bytes_remaining > 0) {
      if (no_fat_chain) {
        cur_cluster++;
      } else {
        auto status_or_next = GetNextCluster(cur_cluster);
        if (!status_or_next.Ok()) {
          kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
          return status_or_next.Status();
        }
        cur_cluster = *status_or_next;
      }
    }
  }

  kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
  return Status::OK;
}

Status ExfatFileSystem::ReadClusters(uint32 first_cluster, bool no_fat_chain,
                                     uint64 offset_in_stream,
                                     uint64 bytes_to_copy, uint8* dest_buffer) {
  return TransferClusters(first_cluster, no_fat_chain, offset_in_stream,
                          bytes_to_copy, dest_buffer, /*is_write=*/false);
}

Status ExfatFileSystem::WriteClusters(uint32 first_cluster, bool no_fat_chain,
                                      uint64 offset_in_stream,
                                      uint64 bytes_to_copy,
                                      const uint8* src_buffer) {
  return TransferClusters(
      first_cluster, no_fat_chain, offset_in_stream, bytes_to_copy,
      const_cast<uint8*>(src_buffer), /*is_write=*/true);
}

StatusOr<uint32> ExfatFileSystem::AllocateClusters(
    uint32 count, bool contiguous, std::vector<uint32>& allocated) {
  if (count == 0) return 0;
  std::lock_guard<std::mutex> lock(fs_mutex_);

  allocated.clear();
  allocated.reserve(count);

  if (contiguous) {
    uint32 consecutive = 0;
    uint32 start_cluster = 0;
    for (uint32 c = 2; c < cluster_count_ + 2; c++) {
      if (!IsClusterAllocated(c)) {
        if (consecutive == 0) start_cluster = c;
        consecutive++;
        if (consecutive == count) {
          for (uint32 k = 0; k < count; k++)
            allocated.push_back(start_cluster + k);
          SetClusterRangeAllocated(start_cluster, count, true);
          FlushClusterBitmapRange(start_cluster, count);
          return start_cluster;
        }
      } else {
        consecutive = 0;
      }
    }
  }

  for (uint32 c = 2; c < cluster_count_ + 2 && allocated.size() < count; c++) {
    if (!IsClusterAllocated(c)) {
      allocated.push_back(c);
      SetClusterAllocated(c, true);
    }
  }

  if (allocated.size() < count) {
    for (uint32 cl : allocated)
      SetClusterAllocated(cl, false);
    return Status::OUT_OF_MEMORY;
  }

  for (size_t i = 0; i < allocated.size(); i++) {
    uint32 next =
        (i + 1 < allocated.size()) ? allocated[i + 1] : kFatEndOfChain;
    SetNextCluster(allocated[i], next);
  }

  FlushAllocationBitmap(0, allocation_bitmap_.size());
  return allocated[0];
}

Status ExfatFileSystem::ExtendClusters(uint32& first_cluster,
                                       bool& no_fat_chain,
                                       uint64 current_data_length,
                                       uint64 new_data_length) {
  uint64 current_allocated = BytesToClusters(current_data_length);
  uint64 needed_clusters = BytesToClusters(new_data_length);

  if (needed_clusters <= current_allocated) return Status::OK;
  uint32 clusters_to_add =
      static_cast<uint32>(needed_clusters - current_allocated);

  if (current_allocated == 0) {
    std::vector<uint32> allocated;
    auto status_or_first = AllocateClusters(clusters_to_add, true, allocated);
    if (!status_or_first.Ok()) return status_or_first.Status();
    first_cluster = *status_or_first;
    no_fat_chain = (allocated.size() == clusters_to_add &&
                    (allocated.back() - allocated.front() + 1 == clusters_to_add));
    return Status::OK;
  }

  if (no_fat_chain) {
    bool can_extend_contiguously = true;
    uint32 start = first_cluster + static_cast<uint32>(current_allocated);
    {
      std::lock_guard<std::mutex> lock(fs_mutex_);
      for (uint32 k = 0; k < clusters_to_add; k++) {
        uint32 cl = start + k;
        if (!IsValidCluster(cl) || IsClusterAllocated(cl)) {
          can_extend_contiguously = false;
          break;
        }
      }
      if (can_extend_contiguously) {
        SetClusterRangeAllocated(start, clusters_to_add, true);
        FlushClusterBitmapRange(start, clusters_to_add);
        return Status::OK;
      }
    }

    for (uint32 i = 0; i < current_allocated - 1; i++)
      SetNextCluster(first_cluster + i, first_cluster + i + 1);

    std::vector<uint32> allocated;
    auto status_or_first = AllocateClusters(clusters_to_add, false, allocated);
    if (!status_or_first.Ok()) return status_or_first.Status();

    SetNextCluster(first_cluster + static_cast<uint32>(current_allocated) - 1,
                   allocated[0]);
    no_fat_chain = false;
    return Status::OK;
  }

  auto status_or_tail = FindLastCluster(first_cluster);
  if (!status_or_tail.Ok()) return status_or_tail.Status();

  std::vector<uint32> allocated;
  auto status_or_first = AllocateClusters(clusters_to_add, false, allocated);
  if (!status_or_first.Ok()) return status_or_first.Status();

  SetNextCluster(*status_or_tail, allocated[0]);
  return Status::OK;
}

Status ExfatFileSystem::FreeClusterChain(uint32 first_cluster,
                                         bool no_fat_chain,
                                         uint64 data_length) {
  if (first_cluster < 2) return Status::OK;
  std::lock_guard<std::mutex> lock(fs_mutex_);

  uint32 cluster_count_to_free =
      static_cast<uint32>(BytesToClusters(data_length));
  uint32 cur = first_cluster;

  for (uint32 i = 0;
       i < cluster_count_to_free && IsValidCluster(cur); i++) {
    SetClusterAllocated(cur, false);

    if (no_fat_chain) {
      cur++;
    } else {
      auto status_or_next = GetNextCluster(cur);
      SetNextCluster(cur, 0);
      if (!status_or_next.Ok()) break;
      cur = *status_or_next;
    }
  }

  FlushAllocationBitmap(0, allocation_bitmap_.size());
  return Status::OK;
}

Status ExfatFileSystem::ReadDirectoryClusters(
    uint32 dir_cluster, std::vector<uint8>& dir_buffer,
    uint32* last_cluster) {
  uint32 cur_cluster = dir_cluster;
  if (last_cluster != nullptr) *last_cluster = dir_cluster;

  while (IsValidCluster(cur_cluster)) {
    if (last_cluster != nullptr) *last_cluster = cur_cluster;
    size_t old_size = dir_buffer.size();
    dir_buffer.resize(old_size + cluster_size_);
    Status status = ReadClusters(cur_cluster, false, 0, cluster_size_,
                                 dir_buffer.data() + old_size);
    if (status != Status::OK) return status;

    auto status_or_next = GetNextCluster(cur_cluster);
    if (!status_or_next.Ok() || IsEndOfChain(*status_or_next)) break;
    cur_cluster = *status_or_next;
  }
  return Status::OK;
}

Status ExfatFileSystem::ForEachRawEntryInDirectory(
    uint32 dir_cluster,
    const std::function<bool(const ExfatEntryInfo&)>& on_entry) {
  std::vector<uint8> dir_buffer;
  Status status = ReadDirectoryClusters(dir_cluster, dir_buffer);
  if (status != Status::OK) return status;

  size_t offset = 0;
  while (offset + sizeof(ExfatDirectoryEntryHeader) <= dir_buffer.size()) {
    uint8 entry_type = dir_buffer[offset];
    if (entry_type == 0x00) break;

    if (entry_type == kEntryTypeAllocationBitmap) {
      const auto* bitmap_entry =
          reinterpret_cast<const ExfatAllocationBitmapEntry*>(
              &dir_buffer[offset]);
      bitmap_first_cluster_ = bitmap_entry->first_cluster;
      bitmap_data_length_ = bitmap_entry->data_length;
      offset += sizeof(ExfatDirectoryEntryHeader);
      continue;
    }

    if (entry_type == kEntryTypeFile) {
      const auto* file_entry =
          reinterpret_cast<const ExfatFileDirectoryEntry*>(&dir_buffer[offset]);
      size_t secondary_count = file_entry->secondary_count;
      bool is_directory =
          (file_entry->file_attributes & kAttributeDirectory) != 0;

      if (offset + (1 + secondary_count) * sizeof(ExfatDirectoryEntryHeader) <=
          dir_buffer.size()) {
        size_t stream_offset = offset + sizeof(ExfatDirectoryEntryHeader);
        if (dir_buffer[stream_offset] == kEntryTypeStreamExtension) {
          const auto* stream_entry =
              reinterpret_cast<const ExfatStreamExtensionEntry*>(
                  &dir_buffer[stream_offset]);
          bool no_fat_chain =
              (stream_entry->general_flags & kStreamFlagNoFatChain) != 0;
          uint8 name_length = stream_entry->name_length;

          std::u16string utf16_name;
          utf16_name.reserve(name_length);
          for (size_t sec = 1; sec < secondary_count; sec++) {
            size_t name_entry_offset =
                offset + (1 + sec) * sizeof(ExfatDirectoryEntryHeader);
            if (dir_buffer[name_entry_offset] == kEntryTypeFileName) {
              const auto* name_entry =
                  reinterpret_cast<const ExfatFileNameEntry*>(
                      &dir_buffer[name_entry_offset]);
              for (size_t ch_idx = 0;
                   ch_idx < 15 && utf16_name.size() < name_length; ch_idx++) {
                utf16_name.push_back(name_entry->file_name[ch_idx]);
              }
            }
          }

          ExfatEntryInfo info;
          info.name = Utf16ToUtf8(utf16_name.data(), utf16_name.size());
          info.is_directory = is_directory;
          info.first_cluster = stream_entry->first_cluster;
          info.data_length = stream_entry->data_length;
          info.no_fat_chain = no_fat_chain;
          info.dir_cluster = dir_cluster;
          info.dir_entry_byte_offset = offset;
          info.secondary_count = secondary_count;

          if (on_entry(info)) return Status::OK;
        }
      }

      offset += (1 + secondary_count) * sizeof(ExfatDirectoryEntryHeader);
    } else {
      offset += sizeof(ExfatDirectoryEntryHeader);
    }
  }

  return Status::OK;
}

Status ExfatFileSystem::FindEntry(std::string_view path,
                                  ExfatEntryInfo& entry_info) {
  while (!path.empty() && path[0] == '/') path = path.substr(1);
  while (!path.empty() && path.back() == '/')
    path = path.substr(0, path.size() - 1);

  if (path.empty()) {
    entry_info.name = "";
    entry_info.is_directory = true;
    entry_info.first_cluster = first_cluster_of_root_dir_;
    entry_info.data_length = 0;
    entry_info.no_fat_chain = false;
    entry_info.dir_cluster = first_cluster_of_root_dir_;
    entry_info.dir_entry_byte_offset = 0;
    entry_info.secondary_count = 0;
    return Status::OK;
  }

  uint32 cur_dir_cluster = first_cluster_of_root_dir_;
  std::string_view remaining = path;

  while (!remaining.empty()) {
    size_t slash = remaining.find('/');
    std::string_view component = (slash == std::string_view::npos)
                                     ? remaining
                                     : remaining.substr(0, slash);
    remaining =
        (slash == std::string_view::npos) ? "" : remaining.substr(slash + 1);

    bool found = false;
    ExfatEntryInfo found_info;

    ForEachRawEntryInDirectory(cur_dir_cluster,
                               [&](const ExfatEntryInfo& info) {
                                 if (EqualsIgnoreCase(info.name, component)) {
                                   found_info = info;
                                   found = true;
                                   return true;
                                 }
                                 return false;
                               });

    if (!found) return Status::FILE_NOT_FOUND;

    if (remaining.empty()) {
      entry_info = found_info;
      return Status::OK;
    }

    if (!found_info.is_directory) return Status::FILE_NOT_FOUND;
    cur_dir_cluster = found_info.first_cluster;
  }

  return Status::FILE_NOT_FOUND;
}

Status ExfatFileSystem::AllocateDirectoryEntries(uint32 dir_cluster,
                                                 size_t entry_count,
                                                 uint64& entry_byte_offset) {
  std::vector<uint8> dir_buffer;
  uint32 last_cluster = dir_cluster;
  Status status = ReadDirectoryClusters(dir_cluster, dir_buffer, &last_cluster);
  if (status != Status::OK) return status;

  size_t consecutive = 0;
  size_t start_slot = 0;
  for (size_t i = 0; i + sizeof(ExfatDirectoryEntryHeader) <= dir_buffer.size();
       i += sizeof(ExfatDirectoryEntryHeader)) {
    uint8 type = dir_buffer[i];
    if (type == 0x00 || (type & 0x80) == 0) {
      if (consecutive == 0) start_slot = i;
      consecutive++;
      if (consecutive == entry_count) {
        entry_byte_offset = start_slot;
        return Status::OK;
      }
    } else {
      consecutive = 0;
    }
  }

  auto status_or_cluster = AllocateZeroedCluster();
  if (!status_or_cluster.Ok()) return status_or_cluster.Status();
  uint32 new_cluster = *status_or_cluster;

  Status set_status = SetNextCluster(last_cluster, new_cluster);
  if (set_status != Status::OK) return set_status;
  set_status = SetNextCluster(new_cluster, kFatEndOfChain);
  if (set_status != Status::OK) return set_status;

  if (consecutive > 0) {
    entry_byte_offset = start_slot;
  } else {
    entry_byte_offset = dir_buffer.size();
  }
  return Status::OK;
}

Status ExfatFileSystem::WriteEntrySet(uint32 dir_cluster,
                                      uint64 entry_byte_offset,
                                      std::string_view name, bool is_directory,
                                      uint32 first_cluster, uint64 data_length,
                                      bool no_fat_chain) {
  std::u16string utf16_name = Utf8ToUtf16(name);
  size_t name_entries = (utf16_name.size() + 14) / 15;
  size_t total_entries = 2 + name_entries;
  std::vector<uint8> entry_set(total_entries * sizeof(ExfatDirectoryEntryHeader), 0);

  auto* file_entry = reinterpret_cast<ExfatFileDirectoryEntry*>(&entry_set[0]);
  file_entry->entry_type = kEntryTypeFile;
  file_entry->secondary_count = static_cast<uint8>(total_entries - 1);
  file_entry->file_attributes = is_directory ? kAttributeDirectory : 0x0020;

  auto* stream_entry = reinterpret_cast<ExfatStreamExtensionEntry*>(
      &entry_set[sizeof(ExfatDirectoryEntryHeader)]);
  stream_entry->entry_type = kEntryTypeStreamExtension;
  stream_entry->general_flags = kStreamFlagAllocationPossible |
                                (no_fat_chain ? kStreamFlagNoFatChain : 0);
  stream_entry->name_length = static_cast<uint8>(utf16_name.size());
  stream_entry->name_hash = ComputeNameHash(utf16_name);
  stream_entry->valid_data_length = data_length;
  stream_entry->first_cluster = first_cluster;
  stream_entry->data_length = data_length;

  for (size_t n = 0; n < name_entries; n++) {
    auto* name_entry = reinterpret_cast<ExfatFileNameEntry*>(
        &entry_set[(2 + n) * sizeof(ExfatDirectoryEntryHeader)]);
    name_entry->entry_type = kEntryTypeFileName;
    for (size_t c = 0; c < 15; c++) {
      size_t char_idx = n * 15 + c;
      name_entry->file_name[c] =
          (char_idx < utf16_name.size()) ? utf16_name[char_idx] : 0;
    }
  }

  FinalizeEntrySetChecksum(entry_set.data(), total_entries);

  return WriteClusters(dir_cluster, false, entry_byte_offset, entry_set.size(),
                       entry_set.data());
}

Status ExfatFileSystem::CreateDirectoryEntry(
    uint32 parent_dir_cluster, std::string_view name, bool is_directory,
    uint32 first_cluster, uint64 data_length, bool no_fat_chain,
    uint64& out_entry_offset, size_t& out_secondary_count) {
  std::u16string utf16_name = Utf8ToUtf16(name);
  size_t name_entries = (utf16_name.size() + 14) / 15;
  size_t total_entries = 2 + name_entries;

  Status status = AllocateDirectoryEntries(parent_dir_cluster, total_entries,
                                           out_entry_offset);
  if (status != Status::OK) return status;

  status = WriteEntrySet(parent_dir_cluster, out_entry_offset, name,
                         is_directory, first_cluster, data_length,
                         no_fat_chain);
  if (status != Status::OK) return status;

  out_secondary_count = total_entries - 1;
  return Status::OK;
}

Status ExfatFileSystem::UpdateFileEntry(uint32 dir_cluster,
                                        uint64 entry_byte_offset,
                                        size_t secondary_count,
                                        uint32 first_cluster,
                                        uint64 data_length, bool no_fat_chain) {
  size_t total_entries = 1 + secondary_count;
  std::vector<uint8> entry_set(total_entries * sizeof(ExfatDirectoryEntryHeader));
  Status status = ReadClusters(dir_cluster, false, entry_byte_offset,
                               entry_set.size(), entry_set.data());
  if (status != Status::OK) return status;

  auto* stream_entry = reinterpret_cast<ExfatStreamExtensionEntry*>(
      &entry_set[sizeof(ExfatDirectoryEntryHeader)]);
  if (no_fat_chain) {
    stream_entry->general_flags |= kStreamFlagNoFatChain;
  } else {
    stream_entry->general_flags &= ~kStreamFlagNoFatChain;
  }
  stream_entry->valid_data_length = data_length;
  stream_entry->first_cluster = first_cluster;
  stream_entry->data_length = data_length;

  FinalizeEntrySetChecksum(entry_set.data(), total_entries);

  return WriteClusters(dir_cluster, false, entry_byte_offset, entry_set.size(),
                       entry_set.data());
}

StatusOr<std::unique_ptr<File>> ExfatFileSystem::OpenFile(
    std::string_view path, size_t& size_in_bytes, ProcessId sender,
    bool read_access, bool write_access, bool create_if_not_exists,
    bool truncate) {
  ExfatEntryInfo info;
  Status status = FindEntry(path, info);

  if (status == Status::FILE_NOT_FOUND) {
    if (write_access && create_if_not_exists) {
      std::string_view dir_path, file_name;
      SplitPath(path, dir_path, file_name);

      ExfatEntryInfo parent_info;
      Status parent_status = FindEntry(dir_path, parent_info);
      if (parent_status != Status::OK || !parent_info.is_directory)
        return Status::FILE_NOT_FOUND;

      uint64 entry_offset = 0;
      size_t secondary_count = 0;
      Status create_status = CreateDirectoryEntry(
          parent_info.first_cluster, file_name, /*is_directory=*/false,
          /*first_cluster=*/0, /*data_length=*/0, /*no_fat_chain=*/true,
          entry_offset, secondary_count);
      if (create_status != Status::OK) return create_status;

      size_in_bytes = 0;
      return std::unique_ptr<File>(std::make_unique<ExfatFile>(
          *this, parent_info.first_cluster, entry_offset, secondary_count, 0,
          0, true, sender));
    }
    return Status::FILE_NOT_FOUND;
  } else if (status != Status::OK) {
    return status;
  }

  if (info.is_directory) return Status::INVALID_ARGUMENT;

  if (truncate && write_access) {
    FreeClusterChain(info.first_cluster, info.no_fat_chain, info.data_length);
    info.first_cluster = 0;
    info.data_length = 0;
    info.no_fat_chain = true;
    UpdateFileEntry(info.dir_cluster, info.dir_entry_byte_offset,
                    info.secondary_count, 0, 0, true);
  }

  size_in_bytes = info.data_length;
  return std::unique_ptr<File>(std::make_unique<ExfatFile>(
      *this, info.dir_cluster, info.dir_entry_byte_offset, info.secondary_count,
      info.first_cluster, info.data_length, info.no_fat_chain, sender));
}

size_t ExfatFileSystem::CountEntriesInDirectory(std::string_view path) {
  ExfatEntryInfo info;
  if (FindEntry(path, info) != Status::OK || !info.is_directory) return 0;

  size_t count = 0;
  ForEachRawEntryInDirectory(info.first_cluster, [&](const ExfatEntryInfo&) {
    count++;
    return false;
  });
  return count;
}

bool ExfatFileSystem::ForEachEntryInDirectory(
    std::string_view path, size_t start_index, size_t count,
    const std::function<void(std::string_view,
                             ::perception::DirectoryEntry::Type, size_t, bool)>&
        on_each_entry) {
  ExfatEntryInfo info;
  if (FindEntry(path, info) != Status::OK || !info.is_directory) return true;

  size_t index = 0;
  bool aborted = false;

  ForEachRawEntryInDirectory(
      info.first_cluster, [&](const ExfatEntryInfo& entry) {
        if (count > 0 && index >= start_index + count) {
          aborted = true;
          return true;
        }
        if (index >= start_index) {
          ::perception::DirectoryEntry::Type type =
              entry.is_directory ? DirectoryEntry::Type::DIRECTORY
                                 : DirectoryEntry::Type::FILE;
          on_each_entry(entry.name, type, entry.data_length, false);
        }
        index++;
        return false;
      });

  return !aborted;
}

std::string_view ExfatFileSystem::GetFileSystemType() const { return kExfatName; }

void ExfatFileSystem::CheckFilePermissions(std::string_view path,
                                           bool& file_exists, bool& can_read,
                                           bool& can_write, bool& can_execute) {
  ExfatEntryInfo info;
  if (FindEntry(path, info) == Status::OK) {
    file_exists = true;
    can_read = true;
    can_write = IsWritable();
    can_execute = true;
  } else {
    file_exists = false;
    can_read = false;
    can_write = false;
    can_execute = false;
  }
}

StatusOr<::perception::FileStatistics> ExfatFileSystem::GetFileStatistics(
    std::string_view path) {
  ExfatEntryInfo info;
  Status status = FindEntry(path, info);
  ::perception::FileStatistics stats;
  stats.optimal_operation_size = optimal_operation_size_;

  if (status != Status::OK) {
    stats.exists = false;
    return stats;
  }

  stats.exists = true;
  stats.type = info.is_directory ? DirectoryEntry::Type::DIRECTORY
                                 : DirectoryEntry::Type::FILE;
  stats.size_in_bytes = info.data_length;
  stats.is_link = false;
  return stats;
}

Status ExfatFileSystem::CreateDirectory(std::string_view path,
                                        ::perception::ProcessId sender) {
  if (!IsWritable()) return Status::NOT_ALLOWED;

  std::string_view dir_path, dir_name;
  SplitPath(path, dir_path, dir_name);

  ExfatEntryInfo parent_info;
  Status status = FindEntry(dir_path, parent_info);
  if (status != Status::OK || !parent_info.is_directory)
    return Status::FILE_NOT_FOUND;

  ExfatEntryInfo existing;
  if (FindEntry(path, existing) == Status::OK) return Status::NOT_ALLOWED;

  auto status_or_cluster = AllocateZeroedCluster();
  if (!status_or_cluster.Ok()) return status_or_cluster.Status();
  uint32 new_cluster = *status_or_cluster;

  uint64 entry_offset = 0;
  size_t secondary_count = 0;
  return CreateDirectoryEntry(parent_info.first_cluster, dir_name,
                              /*is_directory=*/true, new_cluster, cluster_size_,
                              /*no_fat_chain=*/false, entry_offset,
                              secondary_count);
}

Status ExfatFileSystem::DeleteFileOrDirectory(std::string_view path,
                                              ::perception::ProcessId sender) {
  if (!IsWritable()) return Status::NOT_ALLOWED;

  ExfatEntryInfo info;
  Status status = FindEntry(path, info);
  if (status != Status::OK) return status;

  if (info.is_directory && info.first_cluster >= 2) {
    bool has_children = false;
    ForEachRawEntryInDirectory(info.first_cluster, [&](const ExfatEntryInfo&) {
      has_children = true;
      return true;
    });
    if (has_children) return Status::NOT_ALLOWED;
  }

  FreeClusterChain(info.first_cluster, info.no_fat_chain, info.data_length);

  size_t total_entries = 1 + info.secondary_count;
  std::vector<uint8> entry_set(total_entries * sizeof(ExfatDirectoryEntryHeader));
  ReadClusters(info.dir_cluster, false, info.dir_entry_byte_offset,
               entry_set.size(), entry_set.data());

  for (size_t i = 0; i < total_entries; i++)
    entry_set[i * sizeof(ExfatDirectoryEntryHeader)] &= ~0x80;

  return WriteClusters(info.dir_cluster, false, info.dir_entry_byte_offset,
                       entry_set.size(), entry_set.data());
}

std::unique_ptr<FileSystem> InitializeExfatForStorageDevice(
    StorageDevice::Client storage_device, uint64 start_byte_offset,
    uint64 partition_byte_length, std::string_view label) {
  auto pooled = kSharedMemoryPool.GetSharedMemory();

  StorageDeviceReadRequest req;
  req.offset_on_device = start_byte_offset;
  req.offset_in_buffer = 0;
  req.bytes_to_copy = sizeof(ExfatBootSector);
  req.buffer = pooled->shared_memory;

  Status status = storage_device.Read(req);
  if (status != Status::OK) {
    kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
    return nullptr;
  }

  const auto* boot_sector =
      reinterpret_cast<const ExfatBootSector*>(**pooled->shared_memory);

  if (std::memcmp(boot_sector->fs_name, "EXFAT   ", 8) != 0) {
    kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
    return nullptr;
  }

  if (boot_sector->boot_signature != 0xAA55) {
    kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));
    return nullptr;
  }

  uint64 volume_length = boot_sector->volume_length;
  uint32 fat_offset = boot_sector->fat_offset;
  uint32 fat_length = boot_sector->fat_length;
  uint32 cluster_heap_offset = boot_sector->cluster_heap_offset;
  uint32 cluster_count = boot_sector->cluster_count;
  uint32 first_cluster_of_root_dir = boot_sector->root_dir_first_cluster;
  uint8 bytes_per_sector_shift = boot_sector->bytes_per_sector_shift;
  uint8 sectors_per_cluster_shift = boot_sector->sectors_per_cluster_shift;

  kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled));

  if (bytes_per_sector_shift < 9 || bytes_per_sector_shift > 12)
    return nullptr;

  auto fs = std::make_unique<ExfatFileSystem>(
      storage_device, volume_length, fat_offset, fat_length,
      cluster_heap_offset, cluster_count, first_cluster_of_root_dir,
      bytes_per_sector_shift, sectors_per_cluster_shift,
      start_byte_offset);
  if (!label.empty()) fs->SetDeviceName(label);
  return fs;
}

}  // namespace file_systems
