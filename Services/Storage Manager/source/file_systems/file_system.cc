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

#include "file_systems/file_system.h"

#include <cctype>

#include "file_systems/exfat.h"
#include "file_systems/iso9660.h"

using ::perception::devices::StorageDevice;

namespace file_systems {

void SplitPath(std::string_view path, std::string_view& directory,
               std::string_view& file_name) {
  while (!path.empty() && path.back() == '/')
    path = path.substr(0, path.size() - 1);
  size_t split_point = path.find_last_of('/');
  if (split_point == std::string_view::npos) {
    directory = "";
    file_name = path;
  } else {
    directory = path.substr(0, split_point);
    file_name = path.substr(split_point + 1);
  }
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.length() != b.length()) return false;
  for (size_t i = 0; i < a.length(); i++) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

FileSystem::FileSystem() : storage_device_(StorageDevice::Client()) {
  device_name_ = "Ramdisk";
  storage_type_ = ::perception::devices::StorageDeviceType::RAM;
  is_writable_ = true;
  optimal_operation_size_ = 4096;
}

FileSystem::FileSystem(StorageDevice::Client storage_device)
    : storage_device_(storage_device) {
  auto status_or_device_details = storage_device.GetDeviceDetails();
  device_name_ = status_or_device_details->name;
  storage_type_ = status_or_device_details->type;
  is_writable_ = status_or_device_details->is_writable;
  optimal_operation_size_ = status_or_device_details->optimal_operation_size;
}

std::unique_ptr<FileSystem> InitializeStorageDevice(
    StorageDevice::Client storage_device, uint64 start_byte_offset,
    uint64 partition_byte_length, std::string_view label) {
  // Try each known file system to see which one we can initialize.
  if (start_byte_offset == 0) {
    if (auto iso = InitializeIso9960ForStorageDevice(storage_device)) return iso;
  }
  if (auto exfat = InitializeExfatForStorageDevice(
          storage_device, start_byte_offset, partition_byte_length, label))
    return exfat;
  return nullptr;
}

::perception::MountedFileSystemDetails FileSystem::ToMountedFileSystemDetails(
    std::string_view mount_point) const {
  ::perception::MountedFileSystemDetails details;
  details.mount_point =
      mount_point.empty() ? mount_point_ : std::string(mount_point);
  details.device = storage_device_;
  details.start_byte_offset = start_byte_offset_;
  details.byte_length = byte_length_;
  details.device_name = device_name_;
  details.filesystem_type = std::string(GetFileSystemType());
  details.is_writable = is_writable_;
  details.is_boot_drive = is_boot_drive_;
  return details;
}

void FileSystem::NotifyOnDisappearance(
    const std::function<void()>& on_disappearance) {
  if (storage_device_.IsValid())
    storage_device_.NotifyOnDisappearance(on_disappearance);
}

}  // namespace file_systems