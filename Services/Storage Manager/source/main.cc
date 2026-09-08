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
#include <memory>

#include "partitions.h"
#include "perception/devices/storage_device.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/threads.h"
#include "storage_manager.h"
#include "virtual_file_system.h"

using ::perception::HandOverControl;
using ::perception::NotifyOnEachNewServiceInstance;
using ::perception::devices::StorageDevice;

int main(int argc, char *argv[]) {
  ::perception::SetThreadPriority(
      ::perception::ThreadPriority::RealtimeService);
  NotifyOnEachNewServiceInstance<StorageDevice>(
      [](StorageDevice::Client storage_device) {
        bool mounted_any = false;
        OnEachFileSystemOnDevice(
            storage_device,
            [&](std::unique_ptr<file_systems::FileSystem> file_system) {
              MountFileSystem(std::move(file_system));
              mounted_any = true;
            });

        if (!mounted_any) {
          auto status_or_device_details = storage_device.GetDeviceDetails();
          if (status_or_device_details.Ok())
            std::cout << "Unknown file system on "
                      << status_or_device_details->name << "." << std::endl;
        }
      });

  auto storage_manager = std::make_unique<StorageManager>();

  HandOverControl();

  return 0;
}
