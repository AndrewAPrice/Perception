// Copyright 2021 Google LLC
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

#include "file_systems/file_system.h"

class SectorCache;

namespace file_systems {

class Iso9660 : public FileSystem {
 public:
  Iso9660(uint32 size_in_blocks, uint16 logical_block_size,
          std::unique_ptr<char[]> root_directory,
          ::perception::devices::StorageDevice::Client 
        );

  virtual ~Iso9660();

  virtual std::string_view GetFileSystemType() override;

  // Opens a file.
  virtual StatusOr<std::unique_ptr<File>> OpenFile(
      std::string_view path, size_t& size_in_bytes,
      ::perception::ProcessId sender,
      bool read_access, bool write_access,
      bool create_if_not_exists, bool truncate) override;

  // Counts the number of entries in a directory.
  virtual size_t CountEntriesInDirectory(std::string_view path) override;

  // If count is 0, then we will iterate over all of the entries in
  // a directory. Returns if we have no more files in this directory
  // to iterate over, otherwise returns false if we aborted early
  // because we have more entries than what is in 'count'.
  virtual bool ForEachEntryInDirectory(
      std::string_view path, size_t start_index, size_t count,
      const std::function<void(std::string_view,
                               ::perception::DirectoryEntry::Type, size_t,
                               bool)>& on_each_entry) override;

  virtual void CheckFilePermissions(std::string_view path, bool& file_exists,
                                    bool& can_read, bool& can_write,
                                    bool& can_execute) override;

  virtual StatusOr<::perception::FileStatistics> GetFileStatistics(
      std::string_view path) override;

  virtual Status CreateDirectory(std::string_view path, ::perception::ProcessId sender) override;

  virtual Status DeleteFileOrDirectory(std::string_view path, ::perception::ProcessId sender) override;

  // Reads from the device using a sector cache and pre-fetching.
  Status ReadCached(uint64 offset_on_device, uint64 offset_in_buffer,
                    uint64 bytes_to_copy,
                    std::shared_ptr<::perception::SharedMemory> buffer);

  ::perception::devices::StorageDevice::Client GetStorageDevice() const {
    return storage_device_;
  }

 private:
  // Size of the volume, in logical blocks.
  uint32 size_in_blocks_;

  // Logical block size, in bytes.
  uint16 logical_block_size_;

  // Root directory entry.
  std::unique_ptr<char[]> root_directory_;

  // Sector cache for directory and small file reads.
  std::unique_ptr<SectorCache> cache_;

  // Reusable buffer for pre-fetching sectors.
  std::shared_ptr<::perception::SharedMemory> prefetch_buffer_;

  // Mutex to protect the prefetch buffer.
  std::mutex prefetch_mutex_;

  void ForRawEachEntryInDirectory(
      std::string_view path,
      const std::function<bool(std::string_view,
                               ::perception::DirectoryEntry::Type, size_t,
                               size_t)>& on_each_entry);
};

// Returns a FileSystem instance if this device is in the Iso 9660 format.
std::unique_ptr<FileSystem> InitializeIso9960ForStorageDevice(
    ::perception::devices::StorageDevice::Client storage_device);

}  // namespace file_systems
