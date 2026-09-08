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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "file_systems/file_system.h"
#include "perception/devices/storage_device.h"
#include "types.h"

// Information about a detected disk partition.
struct Partition {
  // Byte offset of the partition from the start of the storage device.
  uint64 start_offset = 0;

  // Length of the partition in bytes.
  uint64 length = 0;

  // Name or label of the partition (e.g. from GPT partition entry, if present).
  std::string name;

  // MBR partition type byte (e.g. 0x07 for exFAT/NTFS, 0 if GPT/unassigned).
  uint8_t mbr_type = 0;
};

// Reads partitions from a storage device, supporting MBR and GPT partition tables.
std::vector<Partition> ReadPartitions(
    ::perception::devices::StorageDevice::Client& storage_device);

// Reads partitions using a custom byte reading function.
std::vector<Partition> ReadPartitions(
    uint64 sector_size,
    const std::function<bool(uint64 offset, size_t bytes, void* dest)>& read_bytes);

// Calls the callback for each file system discovered on the storage device
// (either on the raw device or on discovered partitions).
void OnEachFileSystemOnDevice(
    ::perception::devices::StorageDevice::Client storage_device,
    const std::function<void(std::unique_ptr<file_systems::FileSystem>)>&
        on_each_file_system);
