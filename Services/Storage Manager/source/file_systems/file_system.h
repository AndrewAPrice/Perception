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
  FileSystem();

  FileSystem(::perception::devices::StorageDevice::Client storage_device);

  virtual ~FileSystem() {}

  // Opens a file.
  virtual StatusOr<std::unique_ptr<File>> OpenFile(
      std::string_view path, size_t& size_in_bytes,
      ::perception::ProcessId sender, bool read_access, bool write_access,
      bool create_if_not_exists, bool truncate) = 0;

  // Counts the number of entries in a directory.
  virtual size_t CountEntriesInDirectory(std::string_view path) = 0;

  // If count is 0, then we will iterate over all of the entries in
  // a directory. Returns if we have no more files in this directory
  // to iterate over, otherwise returns false if we aborted early
  // because we have more entries than what is in 'count'.
  virtual bool ForEachEntryInDirectory(
      std::string_view path, size_t start_index, size_t count,
      const std::function<void(std::string_view,
                               ::perception::DirectoryEntry::Type, size_t,
                               bool)>& on_each_entry) = 0;

  virtual std::string_view GetFileSystemType() const = 0;

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

  virtual Status CreateDirectory(std::string_view path,
                                 ::perception::ProcessId sender) = 0;

  virtual Status DeleteFileOrDirectory(std::string_view path,
                                       ::perception::ProcessId sender) = 0;

  size_t GetOptionalOperationSize() const { return optimal_operation_size_; }

  const ::perception::devices::StorageDevice::Client& GetStorageDevice() const {
    return storage_device_;
  }

  uint64 GetStartByteOffset() const { return start_byte_offset_; }
  void SetStartByteOffset(uint64 offset) { start_byte_offset_ = offset; }

  uint64 GetByteLength() const { return byte_length_; }
  void SetByteLength(uint64 length) { byte_length_ = length; }

  void SetDeviceName(std::string_view name) { device_name_ = std::string(name); }

  bool IsBootDrive() const { return is_boot_drive_; }
  void SetBootDrive(bool is_boot) { is_boot_drive_ = is_boot; }

  std::string_view GetMountPoint() const { return mount_point_; }
  void SetMountPoint(std::string_view mount_point) {
    mount_point_ = std::string(mount_point);
  }

  // Converts this file system into MountedFileSystemDetails for IPC.
  ::perception::MountedFileSystemDetails ToMountedFileSystemDetails(
      std::string_view mount_point = "") const;

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

  // Start byte offset on the storage device (0 for whole disk, >0 for partition).
  uint64 start_byte_offset_ = 0;

  // Byte length of this filesystem or partition.
  uint64 byte_length_ = 0;

  // Whether this filesystem is the root system boot drive.
  bool is_boot_drive_ = false;

  // The mount point for this file system.
  std::string mount_point_;
};

// Splits path into parent directory and filename.
void SplitPath(std::string_view path, std::string_view& directory,
               std::string_view& file_name);

// Compares two strings case-insensitively.
bool EqualsIgnoreCase(std::string_view a, std::string_view b);

// Returns a FileSystem instance for accessing this storage device if it's
// a file system we can handle, otherwise returns a nullptr.
std::unique_ptr<FileSystem> InitializeStorageDevice(
    ::perception::devices::StorageDevice::Client storage_device,
    uint64 start_byte_offset = 0, uint64 partition_byte_length = 0,
    std::string_view label = "");

}  // namespace file_systems