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

#include "file_systems/ramdisk.h"

#include <cstring>
#include <iostream>

#include "perception/scheduler.h"
#include "virtual_file_system.h"

using ::perception::Defer;
using ::perception::DirectoryEntry;
using ::perception::FileStatistics;
using ::perception::
    GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest;
using ::perception::ProcessId;
using ::perception::ReadFileRequest;
using ::perception::WriteFileRequest;

#include <atomic>

namespace file_systems {

void SplitPath(std::string_view path, std::string& parent, std::string& child) {
  size_t idx = path.find_last_of('/');
  if (idx == std::string_view::npos) {
    parent = "";
    child = std::string(path);
  } else {
    parent = std::string(path.substr(0, idx));
    child = std::string(path.substr(idx + 1));
  }
}

namespace {
std::atomic<int> ramdisk_file_content_instances = 0;
}

RamdiskFileContent::RamdiskFileContent() { ramdisk_file_content_instances++; }

RamdiskFileContent::~RamdiskFileContent() { ramdisk_file_content_instances--; }

int RamdiskFileContent::GetInstances() {
  return ramdisk_file_content_instances.load();
}

namespace {

std::string_view kRamdiskName = "Ramdisk";

// Returns if path is directly inside parent_dir.
// e.g. "Applications/Launcher" is directly inside "Applications".
// "Applications/Launcher/settings.json" is not directly inside "Applications".
bool IsDirectlyInDirectory(std::string_view path, std::string_view parent_dir) {
  if (parent_dir.empty()) {
    return path.find('/') == std::string_view::npos;
  }
  if (path.length() <= parent_dir.length() + 1 ||
      path.substr(0, parent_dir.length()) != parent_dir ||
      path[parent_dir.length()] != '/') {
    return false;
  }
  std::string_view remaining = path.substr(parent_dir.length() + 1);
  return remaining.find('/') == std::string_view::npos;
}

std::string_view GetBaseName(std::string_view path) {
  size_t idx = path.find_last_of('/');
  if (idx == std::string_view::npos) return path;
  return path.substr(idx + 1);
}

class RamdiskFile : public File {
 public:
  RamdiskFile(std::shared_ptr<RamdiskFileContent> content,
              ProcessId allowed_process)
      : content_(content), allowed_process_(allowed_process) {}

  virtual ~RamdiskFile() override {}

  virtual Status Close(ProcessId sender) override {
    if (sender != allowed_process_) return Status::NOT_ALLOWED;
    Defer([sender, this]() { CloseFile(sender, this); });
    return Status::OK;
  }

  virtual Status Read(const ReadFileRequest& request,
                      ProcessId sender) override {
    if (sender != allowed_process_) return Status::NOT_ALLOWED;
    if (request.offset_in_file + request.bytes_to_copy > content_->data.size())
      return Status::OVERFLOW;
    std::memcpy((void*)((size_t)**request.buffer_to_copy_into +
                        request.offset_in_destination_buffer),
                content_->data.data() + request.offset_in_file,
                request.bytes_to_copy);
    return Status::OK;
  }

  virtual Status Write(const WriteFileRequest& request,
                       ProcessId sender) override {
    if (sender != allowed_process_) return Status::NOT_ALLOWED;
    size_t end_offset = request.offset_in_file + request.bytes_to_copy;
    if (end_offset > content_->data.size()) content_->data.resize(end_offset);
    std::memcpy(content_->data.data() + request.offset_in_file,
                (void*)((size_t)**request.buffer_to_copy_from),
                request.bytes_to_copy);
    return Status::OK;
  }

  virtual Status GrantStorageDevicePermissionToAllocateSharedMemoryPages(
      const GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest&
          request,
      ::perception::ProcessId sender) override {
    return Status::OK;
  }

 private:
  std::shared_ptr<RamdiskFileContent> content_;
  ProcessId allowed_process_;
};

}  // namespace

RamdiskFileSystem::RamdiskFileSystem() : FileSystem() {
  directories_[""] = {};
}

RamdiskFileSystem::~RamdiskFileSystem() {}

StatusOr<std::unique_ptr<File>> RamdiskFileSystem::OpenFile(
    std::string_view path, size_t& size_in_bytes,
    ::perception::ProcessId sender, bool read_access, bool write_access,
    bool create_if_not_exists, bool truncate) {
  std::string path_str = std::string(path);

  // If path is tombstoned, it doesn't exist
  if (IsTombstoned(path_str)) {
    if (write_access && create_if_not_exists) {
      ClearTombstone(path_str);
    } else {
      return Status::FILE_NOT_FOUND;
    }
  }

  auto itr = files_.find(path_str);
  if (itr == files_.end()) {
    if (write_access && create_if_not_exists) {
      // Create empty file
      auto content = std::make_shared<RamdiskFileContent>();
      files_[path_str] = content;
      AddChildToParent(path_str, false);
      size_in_bytes = 0;
      return std::unique_ptr<File>(
          std::make_unique<RamdiskFile>(content, sender));
    }
    return Status::FILE_NOT_FOUND;
  }

  if (truncate && write_access) {
    itr->second->data.clear();
  }

  size_in_bytes = itr->second->data.size();
  return std::unique_ptr<File>(
      std::make_unique<RamdiskFile>(itr->second, sender));
}

void RamdiskFileSystem::AddChildToParent(std::string_view path,
                                         bool is_directory) {
  std::string parent, child;
  SplitPath(path, parent, child);
  directories_[parent].insert({child, is_directory});
}

void RamdiskFileSystem::RemoveChildFromParent(std::string_view path) {
  std::string parent, child;
  SplitPath(path, parent, child);
  auto itr = directories_.find(parent);
  if (itr != directories_.end()) {
    itr->second.erase({child, false});
  }
}

void RamdiskFileSystem::RecursivelyDeleteDirectoryContents(
    const std::string& path_str) {
  auto itr = directories_.find(path_str);
  if (itr == directories_.end()) return;

  std::vector<DirectoryChild> children(itr->second.begin(), itr->second.end());
  for (const auto& child : children) {
    std::string child_path =
        path_str.empty() ? child.name : (path_str + "/" + child.name);
    if (child.is_directory) {
      RecursivelyDeleteDirectoryContents(child_path);
      directories_.erase(child_path);
    } else {
      files_.erase(child_path);
    }
  }
}

size_t RamdiskFileSystem::CountEntriesInDirectory(std::string_view path) {
  std::string parent_dir = std::string(path);
  if (!parent_dir.empty() && parent_dir.back() == '/') {
    parent_dir.pop_back();
  }

  auto itr = directories_.find(parent_dir);
  if (itr == directories_.end()) {
    return 0;
  }

  size_t count = 0;
  for (const auto& child : itr->second) {
    std::string child_path =
        parent_dir.empty() ? child.name : (parent_dir + "/" + child.name);
    if (!IsTombstoned(child_path)) {
      count++;
    }
  }
  return count;
}

bool RamdiskFileSystem::ForEachEntryInDirectory(
    std::string_view path, size_t start_index, size_t count,
    const std::function<void(std::string_view,
                             ::perception::DirectoryEntry::Type, size_t, bool)>&
        on_each_entry) {
  std::string parent_dir = std::string(path);
  if (!parent_dir.empty() && parent_dir.back() == '/') parent_dir.pop_back();

  auto itr = directories_.find(parent_dir);
  if (itr == directories_.end()) {
    return true;
  }

  size_t index = 0;
  bool aborted = false;
  for (const auto& child : itr->second) {
    std::string child_path =
        parent_dir.empty() ? child.name : (parent_dir + "/" + child.name);
    if (IsTombstoned(child_path)) {
      continue;
    }

    if (count > 0 && index >= start_index + count) {
      aborted = true;
      break;
    }
    if (index >= start_index) {
      ::perception::DirectoryEntry::Type type =
          child.is_directory ? DirectoryEntry::Type::DIRECTORY
                             : DirectoryEntry::Type::FILE;
      size_t size = 0;
      if (!child.is_directory) {
        auto file_itr = files_.find(child_path);
        if (file_itr != files_.end()) {
          size = file_itr->second->data.size();
        }
      }
      on_each_entry(child.name, type, size, false);
    }
    index++;
  }

  return !aborted;
}

std::string_view RamdiskFileSystem::GetFileSystemType() { return kRamdiskName; }

void RamdiskFileSystem::CheckFilePermissions(std::string_view path,
                                             bool& file_exists, bool& can_read,
                                             bool& can_write,
                                             bool& can_execute) {
  std::string path_str = std::string(path);
  if (IsTombstoned(path_str)) {
    file_exists = false;
    can_read = false;
    can_write = false;
    can_execute = false;
    return;
  }

  if (files_.find(path_str) != files_.end()) {
    file_exists = true;
    can_read = true;
    can_write = true;
    can_execute = true;
    return;
  }

  if (directories_.find(path_str) != directories_.end()) {
    file_exists = true;
    can_read = true;
    can_write = true;
    can_execute = true;
    return;
  }

  file_exists = false;
  can_read = false;
  can_write = false;
  can_execute = false;
}

StatusOr<::perception::FileStatistics> RamdiskFileSystem::GetFileStatistics(
    std::string_view path) {
  ::perception::FileStatistics stats;
  std::string path_str = std::string(path);

  if (IsTombstoned(path_str)) {
    stats.exists = false;
    return stats;
  }

  auto file_itr = files_.find(path_str);
  if (file_itr != files_.end()) {
    stats.exists = true;
    stats.type = DirectoryEntry::Type::FILE;
    stats.size_in_bytes = file_itr->second->data.size();
    stats.optimal_operation_size = optimal_operation_size_;
    stats.is_link = false;
    return stats;
  }

  if (directories_.find(path_str) != directories_.end() || path.empty()) {
    stats.exists = true;
    stats.type = DirectoryEntry::Type::DIRECTORY;
    stats.size_in_bytes = 0;
    stats.optimal_operation_size = optimal_operation_size_;
    stats.is_link = false;
    return stats;
  }

  stats.exists = false;
  return stats;
}

Status RamdiskFileSystem::CreateDirectory(std::string_view path,
                                          ::perception::ProcessId sender) {
  std::string path_str = std::string(path);
  if (IsTombstoned(path_str)) {
    ClearTombstone(path_str);
  }

  if (directories_.find(path_str) != directories_.end()) {
    return Status::OK;  // Already exists
  }

  // Ensure parent directory exists
  size_t idx = path_str.find_last_of('/');
  if (idx != std::string::npos) {
    std::string parent = path_str.substr(0, idx);
    if (!parent.empty() && directories_.find(parent) == directories_.end()) {
      return Status::FILE_NOT_FOUND;  // Parent directory must exist
    }
  }

  directories_[path_str] = {};
  AddChildToParent(path_str, true);
  return Status::OK;
}

Status RamdiskFileSystem::DeleteFileOrDirectory(
    std::string_view path, ::perception::ProcessId sender) {
  std::string path_str = std::string(path);

  auto file_itr = files_.find(path_str);
  if (file_itr != files_.end()) {
    files_.erase(file_itr);
    RemoveChildFromParent(path_str);
    return Status::OK;
  }

  auto dir_itr = directories_.find(path_str);
  if (dir_itr != directories_.end()) {
    RecursivelyDeleteDirectoryContents(path_str);
    directories_.erase(dir_itr);
    RemoveChildFromParent(path_str);
    return Status::OK;
  }

  return Status::FILE_NOT_FOUND;
}

bool RamdiskFileSystem::FileExists(std::string_view path) const {
  std::string path_str = std::string(path);
  return !IsTombstoned(path_str) && files_.find(path_str) != files_.end();
}

bool RamdiskFileSystem::DirectoryExists(std::string_view path) const {
  std::string path_str = std::string(path);
  return !IsTombstoned(path_str) &&
         directories_.find(path_str) != directories_.end();
}

bool RamdiskFileSystem::IsTombstoned(std::string_view path) const {
  return tombstones_.find(std::string(path)) != tombstones_.end();
}

void RamdiskFileSystem::AddTombstone(std::string_view path) {
  tombstones_.insert(std::string(path));
}

void RamdiskFileSystem::ClearTombstone(std::string_view path) {
  tombstones_.erase(std::string(path));
}

void RamdiskFileSystem::CreateFileWithContent(std::string_view path,
                                              std::vector<uint8_t> content) {
  std::string path_str = std::string(path);
  ClearTombstone(path_str);
  auto file_content = std::make_shared<RamdiskFileContent>();
  file_content->data = std::move(content);
  files_[path_str] = file_content;
  AddChildToParent(path_str, false);
}

}  // namespace file_systems
