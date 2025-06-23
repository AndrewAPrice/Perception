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

#include "file_systems/iso9660.h"

using ::perception::devices::StorageDevice;

namespace file_systems {

FileSystem::FileSystem(StorageDevice::Client storage_device)
    : storage_device_(storage_device) {
  auto status_or_device_details = storage_device.GetDeviceDetails();
  device_name_ = status_or_device_details->name;
  storage_type_ = status_or_device_details->type;
  is_writable_ = status_or_device_details->is_writable;
  optimal_operation_size_ = status_or_device_details->optimal_operation_size;
}

std::unique_ptr<FileSystem> InitializeStorageDevice(
    StorageDevice::Client storage_device) {
  // Try each known file system to see which one we can initialize.
  return InitializeIso9960ForStorageDevice(storage_device);
}

void FileSystem::NotifyOnDisappearance(
    const std::function<void()>& on_disappearance) {
  storage_device_.NotifyOnDisappearance(on_disappearance);
}

}  // namespace file_systems