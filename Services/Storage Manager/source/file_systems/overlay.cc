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

#include "file_systems/overlay.h"

#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "perception/shared_memory.h"

using ::perception::DirectoryEntry;
using ::perception::FileStatistics;
using ::perception::ProcessId;
using ::perception::ReadFileRequest;
using ::perception::SharedMemory;

namespace file_systems {
namespace {

std::string kOverlayName = "OVERLAY";

}  // namespace

OverlayFileSystem::OverlayFileSystem(std::unique_ptr<FileSystem> base)
    : FileSystem(),
      base_(std::move(base)),
      overlay_(std::make_unique<RamdiskFileSystem>()) {}

OverlayFileSystem::~OverlayFileSystem() {}

StatusOr<std::unique_ptr<File>> OverlayFileSystem::OpenFile(
    std::string_view path, size_t& size_in_bytes,
    ::perception::ProcessId sender, bool read_access, bool write_access,
    bool create_if_not_exists, bool truncate) {
  std::string path_str = std::string(path);
  bool exists_in_overlay = overlay_->FileExists(path_str);
  bool tombstoned = overlay_->IsTombstoned(path_str);

  if (tombstoned) {
    if (write_access && create_if_not_exists) {
      overlay_->ClearTombstone(path_str);
      tombstoned = false;
      exists_in_overlay = false;
    } else {
      return Status::FILE_NOT_FOUND;
    }
  }

  if (write_access) {
    if (exists_in_overlay) {
      return overlay_->OpenFile(path_str, size_in_bytes, sender, read_access,
                                write_access, create_if_not_exists, truncate);
    }

    // Check if it exists in base
    bool exists_in_base = false;
    size_t base_size = 0;
    if (!tombstoned) {
      auto stats = base_->GetFileStatistics(path_str);
      if (stats.Ok() && stats->exists &&
          stats->type == DirectoryEntry::Type::FILE) {
        exists_in_base = true;
        base_size = stats->size_in_bytes;
      }
    }

    if (exists_in_base) {
      // Copy on Write!
      auto shared_memory = SharedMemory::FromSize(base_size, 0);
      if (!shared_memory) {
        return Status::OUT_OF_MEMORY;
      }

      size_t dummy_size;
      auto base_file_or = base_->OpenFile(path_str, dummy_size, sender, true,
                                          false, false, false);
      if (!base_file_or.Ok()) return base_file_or.Status();
      auto base_file = std::move(*base_file_or);

      ReadFileRequest read_request;
      read_request.offset_in_file = 0;
      read_request.offset_in_destination_buffer = 0;
      read_request.bytes_to_copy = base_size;
      read_request.buffer_to_copy_into = shared_memory;

      Status status = base_file->Read(read_request, sender);
      if (status != Status::OK) {
        return status;
      }

      std::vector<uint8_t> content(base_size);
      if (base_size > 0) {
        std::memcpy(content.data(), **shared_memory, base_size);
      }
      overlay_->CreateFileWithContent(path_str, std::move(content));

      return overlay_->OpenFile(path_str, size_in_bytes, sender, read_access,
                                write_access, create_if_not_exists, truncate);
    } else if (create_if_not_exists) {
      overlay_->CreateFileWithContent(path_str, {});
      return overlay_->OpenFile(path_str, size_in_bytes, sender, read_access,
                                write_access, create_if_not_exists, truncate);
    } else {
      return Status::FILE_NOT_FOUND;
    }
  } else {
    // Read only
    if (exists_in_overlay) {
      return overlay_->OpenFile(path_str, size_in_bytes, sender, read_access,
                                write_access, create_if_not_exists, truncate);
    }
    if (!tombstoned) {
      return base_->OpenFile(path_str, size_in_bytes, sender, read_access,
                             write_access, create_if_not_exists, truncate);
    }
    return Status::FILE_NOT_FOUND;
  }
}

size_t OverlayFileSystem::CountEntriesInDirectory(std::string_view path) {
  // Merge and count
  std::string parent_dir = std::string(path);
  if (!parent_dir.empty() && parent_dir.back() == '/') {
    parent_dir.pop_back();
  }

  std::set<std::string> names;

  // Add from base
  base_->ForEachEntryInDirectory(
      parent_dir, 0, 0,
      [&](std::string_view name, DirectoryEntry::Type type, size_t size,
          bool is_link) {
        std::string entry_path = parent_dir.empty()
                                     ? std::string(name)
                                     : (parent_dir + "/" + std::string(name));
        if (!overlay_->IsTombstoned(entry_path)) {
          names.insert(std::string(name));
        }
      });

  // Add from overlay
  overlay_->ForEachEntryInDirectory(
      parent_dir, 0, 0,
      [&](std::string_view name, DirectoryEntry::Type type, size_t size,
          bool is_link) { names.insert(std::string(name)); });

  return names.size();
}

bool OverlayFileSystem::ForEachEntryInDirectory(
    std::string_view path, size_t start_index, size_t count,
    const std::function<void(std::string_view,
                             ::perception::DirectoryEntry::Type, size_t, bool)>&
        on_each_entry) {
  std::string parent_dir = std::string(path);
  if (!parent_dir.empty() && parent_dir.back() == '/') {
    parent_dir.pop_back();
  }

  struct MergedEntry {
    std::string name;
    DirectoryEntry::Type type;
    size_t size;
    bool is_link;
  };
  std::map<std::string, MergedEntry> merged_entries;

  // 1. Read from base
  base_->ForEachEntryInDirectory(
      parent_dir, 0, 0,
      [&](std::string_view name, DirectoryEntry::Type type, size_t size,
          bool is_link) {
        std::string entry_path = parent_dir.empty()
                                     ? std::string(name)
                                     : (parent_dir + "/" + std::string(name));
        if (!overlay_->IsTombstoned(entry_path)) {
          merged_entries[std::string(name)] = {std::string(name), type, size,
                                               is_link};
        }
      });

  // 2. Read from overlay (overrides base)
  overlay_->ForEachEntryInDirectory(
      parent_dir, 0, 0,
      [&](std::string_view name, DirectoryEntry::Type type, size_t size,
          bool is_link) {
        merged_entries[std::string(name)] = {std::string(name), type, size,
                                             is_link};
      });

  // Pagination
  size_t index = 0;
  bool aborted = false;
  for (const auto& pair : merged_entries) {
    if (count > 0 && index >= start_index + count) {
      aborted = true;
      break;
    }
    if (index >= start_index) {
      on_each_entry(pair.second.name, pair.second.type, pair.second.size,
                    pair.second.is_link);
    }
    index++;
  }

  return !aborted;
}

std::string_view OverlayFileSystem::GetFileSystemType() { return kOverlayName; }

void OverlayFileSystem::CheckFilePermissions(std::string_view path,
                                             bool& file_exists, bool& can_read,
                                             bool& can_write,
                                             bool& can_execute) {
  std::string path_str = std::string(path);
  if (overlay_->IsTombstoned(path_str)) {
    file_exists = false;
    can_read = false;
    can_write = false;
    can_execute = false;
    return;
  }

  if (overlay_->FileExists(path_str) || overlay_->DirectoryExists(path_str)) {
    overlay_->CheckFilePermissions(path_str, file_exists, can_read, can_write,
                                   can_execute);
    return;
  }

  base_->CheckFilePermissions(path_str, file_exists, can_read, can_write,
                              can_execute);
}

StatusOr<::perception::FileStatistics> OverlayFileSystem::GetFileStatistics(
    std::string_view path) {
  std::string path_str = std::string(path);
  if (overlay_->IsTombstoned(path_str)) {
    ::perception::FileStatistics stats;
    stats.exists = false;
    return stats;
  }

  if (overlay_->FileExists(path_str) || overlay_->DirectoryExists(path_str))
    return overlay_->GetFileStatistics(path_str);

  return base_->GetFileStatistics(path_str);
}

Status OverlayFileSystem::CreateDirectory(std::string_view path,
                                          ::perception::ProcessId sender) {
  std::string path_str = std::string(path);
  if (overlay_->IsTombstoned(path_str)) overlay_->ClearTombstone(path_str);

  return overlay_->CreateDirectory(path_str, sender);
}

Status OverlayFileSystem::DeleteFileOrDirectory(
    std::string_view path, ::perception::ProcessId sender) {
  std::string path_str = std::string(path);

  // If it exists in overlay, delete it from overlay
  bool deleted = false;
  if (overlay_->FileExists(path_str) || overlay_->DirectoryExists(path_str)) {
    Status status = overlay_->DeleteFileOrDirectory(path_str, sender);
    if (status != Status::OK) return status;
    deleted = true;
  }

  // If it exists in base, tombstone it
  auto stats = base_->GetFileStatistics(path_str);
  if (stats.Ok() && stats->exists) {
    overlay_->AddTombstone(path_str);
    deleted = true;
  }

  return deleted ? Status::OK : Status::FILE_NOT_FOUND;
}

}  // namespace file_systems
