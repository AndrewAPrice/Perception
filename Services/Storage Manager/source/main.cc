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

#include <iostream>

#include "file_systems/file_system.h"
#include "perception/devices/storage_device.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "storage_manager.h"
#include "virtual_file_system.h"

using ::file_systems::InitializeStorageDevice;
using ::perception::HandOverControl;
using ::perception::NotifyOnEachNewServiceInstance;
using ::perception::devices::StorageDevice;

int main(int argc, char *argv[]) {
  NotifyOnEachNewServiceInstance<StorageDevice>(
      [](StorageDevice::Client storage_device) {
        auto file_system = InitializeStorageDevice(storage_device);
        if (file_system) {
          MountFileSystem(std::move(file_system));
        } else if (!file_system) {
          auto status_or_device_details = storage_device.GetDeviceDetails();
          std::cout << "Unknown file system on "
                    << status_or_device_details->name << "." << std::endl;
        }
      });

  auto storage_manager = std::make_unique<StorageManager>();

  HandOverControl();

  return 0;
}
