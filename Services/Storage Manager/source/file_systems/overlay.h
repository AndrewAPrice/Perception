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

#include "file_systems/file_system.h"
#include "file_systems/ramdisk.h"

namespace file_systems {

// An overlay, that wraps a read-only file system where writes are stored to
// ramdisk.
class OverlayFileSystem : public FileSystem {
 public:
  OverlayFileSystem(std::unique_ptr<FileSystem> base);
  virtual ~OverlayFileSystem() override;

  virtual StatusOr<std::unique_ptr<File>> OpenFile(
      std::string_view path, size_t& size_in_bytes,
      ::perception::ProcessId sender, bool read_access, bool write_access,
      bool create_if_not_exists, bool truncate) override;

  virtual size_t CountEntriesInDirectory(std::string_view path) override;

  virtual bool ForEachEntryInDirectory(
      std::string_view path, size_t start_index, size_t count,
      const std::function<void(std::string_view,
                               ::perception::DirectoryEntry::Type, size_t,
                               bool)>& on_each_entry) override;

  virtual std::string_view GetFileSystemType() override;

  virtual void CheckFilePermissions(std::string_view path, bool& file_exists,
                                    bool& can_read, bool& can_write,
                                    bool& can_execute) override;

  virtual StatusOr<::perception::FileStatistics> GetFileStatistics(
      std::string_view path) override;

  virtual Status CreateDirectory(std::string_view path,
                                 ::perception::ProcessId sender) override;
  virtual Status DeleteFileOrDirectory(std::string_view path,
                                       ::perception::ProcessId sender) override;

 private:
  std::unique_ptr<FileSystem> base_;
  std::unique_ptr<RamdiskFileSystem> overlay_;
};

}  // namespace file_systems
