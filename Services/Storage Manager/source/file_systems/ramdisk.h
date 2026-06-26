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

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "file_systems/file_system.h"

namespace file_systems {

struct RamdiskFileContent {
  RamdiskFileContent();
  ~RamdiskFileContent();
  std::vector<uint8_t> data;
  static int GetInstances();
};

class RamdiskFileSystem : public FileSystem {
 public:
  RamdiskFileSystem();
  virtual ~RamdiskFileSystem() override;

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

  // Extra helper methods for overlay
  bool FileExists(std::string_view path) const;
  bool DirectoryExists(std::string_view path) const;
  bool IsTombstoned(std::string_view path) const;
  void AddTombstone(std::string_view path);
  void ClearTombstone(std::string_view path);
  void CreateFileWithContent(std::string_view path,
                             std::vector<uint8_t> content);

 private:
  struct DirectoryChild {
    std::string name;
    bool is_directory;

    bool operator<(const DirectoryChild& other) const {
      return name < other.name;
    }
  };

  void AddChildToParent(std::string_view path, bool is_directory);
  void RemoveChildFromParent(std::string_view path);
  void RecursivelyDeleteDirectoryContents(const std::string& path_str);

  std::map<std::string, std::shared_ptr<RamdiskFileContent>> files_;
  std::set<std::string> tombstones_;
  std::unordered_map<std::string, std::set<DirectoryChild>> directories_;
};

}  // namespace file_systems
