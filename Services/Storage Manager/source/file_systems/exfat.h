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

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "file_systems/file_system.h"
#include "perception/devices/storage_device.h"
#include "perception/shared_memory.h"
#include "sector_cache.h"

namespace file_systems {

struct ExfatEntryInfo {
  std::string name;
  bool is_directory;
  uint32 first_cluster;
  uint64 data_length;
  bool no_fat_chain;
  uint64 dir_cluster;
  uint64 dir_entry_byte_offset;
  size_t secondary_count;
};

class ExfatFileSystem : public FileSystem {
 public:
  ExfatFileSystem(::perception::devices::StorageDevice::Client storage_device,
                  uint64 volume_length, uint32 fat_offset, uint32 fat_length,
                  uint32 cluster_heap_offset, uint32 cluster_count,
                  uint32 first_cluster_of_root_dir,
                  uint8 bytes_per_sector_shift,
                  uint8 sectors_per_cluster_shift,
                  uint64 start_byte_offset = 0);

  virtual ~ExfatFileSystem() override;

  // Opens a file.
  virtual StatusOr<std::unique_ptr<File>> OpenFile(
      std::string_view path, size_t& size_in_bytes,
      ::perception::ProcessId sender, bool read_access, bool write_access,
      bool create_if_not_exists, bool truncate) override;

  // Counts the number of entries in a directory.
  virtual size_t CountEntriesInDirectory(std::string_view path) override;

  // Iterates over all of the entries in a directory.
  virtual bool ForEachEntryInDirectory(
      std::string_view path, size_t start_index, size_t count,
      const std::function<void(std::string_view,
                               ::perception::DirectoryEntry::Type, size_t,
                               bool)>& on_each_entry) override;

  // Returns the name of the file system type.
  virtual std::string_view GetFileSystemType() const override;

  // Checks the permissions of a file.
  virtual void CheckFilePermissions(std::string_view path, bool& file_exists,
                                    bool& can_read, bool& can_write,
                                    bool& can_execute) override;

  // Gets statistics about a file.
  virtual StatusOr<::perception::FileStatistics> GetFileStatistics(
      std::string_view path) override;

  // Creates a directory.
  virtual Status CreateDirectory(std::string_view path,
                                 ::perception::ProcessId sender) override;

  // Deletes a file or directory.
  virtual Status DeleteFileOrDirectory(std::string_view path,
                                       ::perception::ProcessId sender) override;

  // Reads data from cluster chain.
  Status ReadClusters(uint32 first_cluster, bool no_fat_chain,
                      uint64 offset_in_stream, uint64 bytes_to_copy,
                      uint8* dest_buffer);

  // Writes data to cluster chain.
  Status WriteClusters(uint32 first_cluster, bool no_fat_chain,
                       uint64 offset_in_stream, uint64 bytes_to_copy,
                       const uint8* src_buffer);

  // Transfers data to or from a cluster chain.
  Status TransferClusters(uint32 first_cluster, bool no_fat_chain,
                          uint64 offset_in_stream, uint64 bytes_to_copy,
                          uint8* buffer, bool is_write);

  // Allocates clusters and updates FAT if needed.
  StatusOr<uint32> AllocateClusters(uint32 count, bool contiguous,
                                    std::vector<uint32>& allocated);

  // Extends an existing cluster allocation for a file.
  Status ExtendClusters(uint32& first_cluster, bool& no_fat_chain,
                        uint64 current_data_length, uint64 new_data_length);

  // Frees a cluster chain.
  Status FreeClusterChain(uint32 first_cluster, bool no_fat_chain,
                          uint64 data_length);

  // Updates file entry data length and first cluster on disk.
  Status UpdateFileEntry(uint32 dir_cluster, uint64 entry_byte_offset,
                         size_t secondary_count, uint32 first_cluster,
                         uint64 data_length, bool no_fat_chain);

  // Returns storage device reference.
  ::perception::devices::StorageDevice::Client& GetStorageDevice() {
    return storage_device_;
  }

  // Returns cluster size in bytes.
  uint32 GetClusterSize() const { return cluster_size_; }

 private:
  uint64 volume_length_;
  uint32 fat_offset_;
  uint32 fat_length_;
  uint32 cluster_heap_offset_;
  uint32 cluster_count_;
  uint32 first_cluster_of_root_dir_;
  uint8 bytes_per_sector_shift_;
  uint8 sectors_per_cluster_shift_;

  uint32 sector_size_;
  uint32 cluster_size_;
  uint32 sectors_per_cluster_;

  uint32 bitmap_first_cluster_;
  uint64 bitmap_data_length_;

  std::mutex fs_mutex_;
  std::vector<uint8> allocation_bitmap_;

  // Converts a byte count to the number of clusters needed.
  uint64 BytesToClusters(uint64 bytes) const {
    return (bytes + cluster_size_ - 1) / cluster_size_;
  }

  // Checks if a cluster number is valid.
  bool IsValidCluster(uint32 cluster) const {
    return cluster >= 2 && cluster < cluster_count_ + 2;
  }

  // Checks if a cluster number marks the end of a chain or an invalid cluster.
  bool IsEndOfChain(uint32 cluster) const {
    return cluster < 2 || cluster >= 0xFFFFFFF7;
  }

  // Computes sector offset on storage device for a given cluster.
  uint64 ClusterToDeviceOffset(uint32 cluster) const;

  // Computes device offset for a FAT entry.
  uint64 FatEntryToDeviceOffset(uint32 cluster) const;

  // Reads the next cluster in FAT chain.
  StatusOr<uint32> GetNextCluster(uint32 cluster);

  // Sets the next cluster in FAT chain.
  Status SetNextCluster(uint32 cluster, uint32 next_cluster);

  // Finds the last cluster in a FAT chain.
  StatusOr<uint32> FindLastCluster(uint32 first_cluster);

  // Allocates a single cluster and clears it with zeros.
  StatusOr<uint32> AllocateZeroedCluster();

  // Checks if a cluster is marked as allocated in the bitmap.
  bool IsClusterAllocated(uint32 cluster) const;

  // Marks a cluster as allocated or free in the bitmap.
  void SetClusterAllocated(uint32 cluster, bool allocated);

  // Marks a range of contiguous clusters as allocated or free in the bitmap.
  void SetClusterRangeAllocated(uint32 start_cluster, uint32 count,
                               bool allocated);

  // Flushes the bitmap byte range covering the given clusters.
  Status FlushClusterBitmapRange(uint32 start_cluster, uint32 count);

  // Loads allocation bitmap into memory.
  Status LoadAllocationBitmap();

  // Flushes modified portion of allocation bitmap to disk.
  Status FlushAllocationBitmap(size_t byte_start, size_t byte_length);

  // Resolves a path to directory entry info.
  Status FindEntry(std::string_view path, ExfatEntryInfo& entry_info);

  // Reads all clusters belonging to a directory into a buffer.
  Status ReadDirectoryClusters(uint32 dir_cluster,
                               std::vector<uint8>& dir_buffer,
                               uint32* last_cluster = nullptr);

  // Scans directory entries in a directory.
  Status ForEachRawEntryInDirectory(
      uint32 dir_cluster,
      const std::function<bool(const ExfatEntryInfo&)>& on_entry);

  // Finds or allocates free directory entry slots in a directory.
  Status AllocateDirectoryEntries(uint32 dir_cluster, size_t entry_count,
                                  uint64& entry_byte_offset);

  // Writes entry set into directory.
  Status WriteEntrySet(uint32 dir_cluster, uint64 entry_byte_offset,
                       std::string_view name, bool is_directory,
                       uint32 first_cluster, uint64 data_length,
                       bool no_fat_chain);

  // Allocates directory entry slots and writes the entry set.
  Status CreateDirectoryEntry(uint32 parent_dir_cluster, std::string_view name,
                              bool is_directory, uint32 first_cluster,
                              uint64 data_length, bool no_fat_chain,
                              uint64& out_entry_offset,
                              size_t& out_secondary_count);
};

// Initializes exFAT file system for a given storage device.
std::unique_ptr<FileSystem> InitializeExfatForStorageDevice(
    ::perception::devices::StorageDevice::Client storage_device,
    uint64 start_byte_offset = 0, uint64 partition_byte_length = 0,
    std::string_view label = "");

}  // namespace file_systems
