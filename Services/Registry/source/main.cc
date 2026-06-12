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

#include "mount_listener.h"
#include "multiboot_loader.h"
#include "perception/services.h"
#include "perception/storage_manager.h"
#include "registry_server.h"

using perception::NotifyOnEachNewServiceInstance;
using perception::StorageManager;

int main() {
  // Load the multiboot registry.
  LoadMultibootRegistry();

  // Register the mount listener to update settings as they come in.
  MountListener mount_listener;
  NotifyOnEachNewServiceInstance<StorageManager>(
      [&mount_listener](StorageManager::Client storage_device) {
        storage_device.ListenForMounts(mount_listener);
      });

  // Start the Registry server.
  RegistryServer server;

  ::perception::HandOverControl();
  return 0;
}
