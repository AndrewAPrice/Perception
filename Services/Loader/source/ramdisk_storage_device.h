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

#include "perception/devices/storage_device.h"
#include "perception/multiboot.h"

// Implements a StorageDevice server backed by an in-memory multiboot module.
class RamdiskStorageDevice
    : public ::perception::devices::StorageDevice::Server {
 public:
  // Constructs the ramdisk storage device around a multiboot module.
  explicit RamdiskStorageDevice(
      std::unique_ptr<::perception::MultibootModule> module);

  virtual ~RamdiskStorageDevice() override = default;

  // Returns details of this storage device.
  virtual StatusOr<::perception::devices::StorageDeviceDetails>
  GetDeviceDetails() override;

  // Reads data from the ramdisk buffer into the provided shared memory buffer.
  virtual Status Read(
      const ::perception::devices::StorageDeviceReadRequest& request) override;

  // Writes are not supported on the read-only ramdisk image.
  virtual Status Write(
      const ::perception::devices::StorageDeviceWriteRequest& request) override;

 private:
  // The in-memory multiboot module buffer containing the filesystem image.
  std::unique_ptr<::perception::MultibootModule> module_;
};
