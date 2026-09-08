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

#include <cstddef>
#include <cstdint>
#include <span>

#include "perception/disk/disk_manager.h"
#include "perception/disk/disk_structures.h"

namespace perception {
namespace disk {

// Installs GRUB 2 BIOS bootloader to an MBR partitioned disk.
// Writes patched boot.img to sector 0 (preserving partition table) and core.img
// to the MBR gap (sector 1).
bool InstallGrubToMbrDisk(DiskInfo& disk, uint64_t target_partition_start_lba,
                          std::span<const std::byte> boot_img,
                          std::span<const std::byte> core_img,
                          DiskManager& disk_manager);

// Installs GRUB 2 BIOS bootloader to a GPT partitioned disk.
// Writes patched boot.img to sector 0 (preserving protective MBR) and core.img
// to the BIOS Boot Partition.
bool InstallGrubToGptDisk(DiskInfo& disk, uint64_t bios_boot_start_lba,
                          std::span<const std::byte> boot_img,
                          std::span<const std::byte> core_img,
                          DiskManager& disk_manager);

// Finds an existing BIOS Boot Partition on a GPT disk, or creates one if
// unallocated space permits.
bool EnsureGptBiosBootPartition(DiskInfo& disk,
                                uint64_t& out_bios_boot_start_lba,
                                DiskManager& disk_manager);

}  // namespace disk
}  // namespace perception
