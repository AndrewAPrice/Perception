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

#include "perception/disk/formatter.h"

#include "perception/disk/filesystems/exfat.h"

namespace perception {
namespace disk {

bool FormatFilesystem(FilesystemType type, uint64_t start_lba,
                      uint64_t sector_count, uint32_t sector_size,
                      const std::string& volume_label,
                      const BlockWriter& writer) {
  switch (type) {
    case FilesystemType::EXFAT:
      return filesystems::FormatExfat(start_lba, sector_count, sector_size,
                                      volume_label, writer);
    default:
      return false;
  }
}

}  // namespace disk
}  // namespace perception
