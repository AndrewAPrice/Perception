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

#include <functional>
#include <memory>
#include <string_view>

#include "file.h"
#include "perception/devices/storage_device.h"
#include "perception/storage_manager.h"

namespace file_systems {

class FileSystem {
 public:
  FileSystem(::perception::devices::StorageDevice::Client storage_device);

  virtual ~FileSystem() {}

  // Opens a file.
  virtual StatusOr<std::unique_ptr<File>> OpenFile(
      std::string_view path, size_t& size_in_bytes,
      ::perception::ProcessId sender) = 0;

  // Counts the number of entries in a directory.
  virtual size_t CountEntriesInDirectory(std::string_view path) = 0;

  // If count is 0, then we will iterate over all of the entries in
  // a directory. Returns if we have no more files in this directory
  // to iterate over, otherwise returns false if we aborted early
  // because we have more entries than what is in 'count'.
  virtual bool ForEachEntryInDirectory(
      std::string_view path, size_t start_index, size_t count,
      const std::function<void(std::string_view,
                               ::perception::DirectoryEntry::Type, size_t)>&
          on_each_entry) = 0;

  virtual std::string_view GetFileSystemType() = 0;

  ::perception::devices::StorageDeviceType GetStorageType() const {
    return storage_type_;
  }

  std::string_view GetDeviceName() const { return device_name_; }

  bool IsWritable() const { return is_writable_; }

  virtual void CheckFilePermissions(std::string_view path, bool& file_exists,
                                    bool& can_read, bool& can_write,
                                    bool& can_execute) = 0;

  virtual StatusOr<::perception::FileStatistics> GetFileStatistics(
      std::string_view path) = 0;

  size_t GetOptionalOperationSize() const { return optimal_operation_size_; }

  void NotifyOnDisappearance(const std::function<void()>& on_disappearance);

 protected:
  // Storage device.
  ::perception::devices::StorageDevice::Client storage_device_;

  // The type of storage device this is.
  ::perception::devices::StorageDeviceType storage_type_;

  // The name of the device.
  std::string device_name_;

  // Is this device writable?
  bool is_writable_;

  // The optimal size for operations, in bytes.
  size_t optimal_operation_size_;
};

// Returns a FileSystem instance for accessing this storage device if it's
// a file system we can handle, otherwise returns a nullptr.
std::unique_ptr<FileSystem> InitializeStorageDevice(
    ::perception::devices::StorageDevice::Client storage_device);

}  // namespace file_systems