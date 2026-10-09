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

#include "file_operations.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <system_error>

namespace {

// Buffer size in bytes for chunked file copy operations.
constexpr size_t kFileCopyChunkSize = 65536;

}  // namespace

std::string GetExtension(std::string_view name) {
  std::string ext = std::filesystem::path(name).extension().string();
  if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

std::string JoinPath(std::string_view dir, std::string_view name) {
  if (dir.empty() || dir == "/") return "/" + std::string(name);
  if (dir.back() == '/') return std::string(dir) + std::string(name);
  return std::string(dir) + "/" + std::string(name);
}

std::string GetUniquePath(std::string_view dir, std::string_view desired_name,
                          bool force_suffix) {
  std::error_code ec;
  std::string candidate = JoinPath(dir, desired_name);
  if (!force_suffix && !std::filesystem::exists(candidate, ec))
    return candidate;

  std::filesystem::path name_path(desired_name);
  std::string stem = name_path.stem().string();
  std::string ext = name_path.extension().string();
  if (stem.empty()) {
    stem = std::string(desired_name);
    ext.clear();
  }

  int index = 1;
  while (true) {
    std::string numbered_name =
        stem + " (" + std::to_string(index) + ")" + ext;
    candidate = JoinPath(dir, numbered_name);
    if (!std::filesystem::exists(candidate, ec)) return candidate;
    index++;
  }
}

std::string NormalizeAndResolveDirectoryPath(std::string_view path) {
  std::string target_path(path);
  if (target_path.empty()) target_path = "/";

  if (target_path.size() > 1 && target_path.back() == '/')
    target_path.pop_back();

  std::error_code ec;
  if (std::filesystem::is_symlink(target_path, ec)) {
    auto resolved = std::filesystem::read_symlink(target_path, ec);
    if (!ec && !resolved.empty()) target_path = resolved.string();
  }
  return target_path;
}

bool CopyFileChunked(const std::string& src_path, const std::string& dst_path,
                     std::string& error_message) {
  std::ifstream src(src_path, std::ios::binary);
  if (!src.is_open()) {
    error_message = "Failed to open source file: " + src_path;
    return false;
  }

  std::ofstream dst(dst_path, std::ios::binary | std::ios::trunc);
  if (!dst.is_open()) {
    error_message = "Failed to create destination file: " + dst_path;
    return false;
  }

  std::vector<char> buffer(kFileCopyChunkSize);
  while (src) {
    src.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::streamsize bytes_read = src.gcount();
    if (bytes_read > 0) {
      dst.write(buffer.data(), bytes_read);
      if (!dst.good()) {
        error_message = "Failed while writing to: " + dst_path;
        return false;
      }
    }
  }

  dst.flush();
  if (!dst.good()) {
    error_message = "Failed to finalize file: " + dst_path;
    return false;
  }
  return true;
}

bool CopyItemRecursive(const std::string& src_path, const std::string& dst_path,
                       std::string& error_message) {
  std::error_code ec;
  if (std::filesystem::is_directory(src_path, ec)) {
    if (!std::filesystem::exists(dst_path, ec)) {
      if (!std::filesystem::create_directory(dst_path, ec) && ec) {
        error_message = ec.message();
        return false;
      }
    }
    std::vector<std::filesystem::directory_entry> children;
    for (const auto& entry : std::filesystem::directory_iterator(src_path, ec))
      children.push_back(entry);
    if (ec) {
      error_message = ec.message();
      return false;
    }
    for (const auto& child : children) {
      std::string child_name = child.path().filename().string();
      std::string child_dst = JoinPath(dst_path, child_name);
      if (!CopyItemRecursive(child.path().string(), child_dst, error_message))
        return false;
    }
    return true;
  }

  return CopyFileChunked(src_path, dst_path, error_message);
}

bool DeleteItemRecursive(const std::string& target_path,
                         std::string& error_message) {
  std::error_code ec;
  if (std::filesystem::is_directory(target_path, ec) &&
      !std::filesystem::is_symlink(target_path, ec)) {
    std::vector<std::filesystem::directory_entry> children;
    for (const auto& entry :
         std::filesystem::directory_iterator(target_path, ec))
      children.push_back(entry);
    if (ec) {
      error_message = ec.message();
      return false;
    }
    for (const auto& child : children) {
      if (!DeleteItemRecursive(child.path().string(), error_message))
        return false;
    }
  }

  std::filesystem::remove(target_path, ec);
  if (ec) {
    error_message = ec.message();
    return false;
  }
  return true;
}

bool MoveOrRenameItem(const std::string& src_path, const std::string& dst_path,
                      std::string& error_message) {
  if (!CopyItemRecursive(src_path, dst_path, error_message)) return false;
  return DeleteItemRecursive(src_path, error_message);
}

void SortDirectoryEntries(
    std::vector<std::filesystem::directory_entry>& entries,
    SortColumn sort_column, SortDirection sort_direction) {
  auto compare_entries = [sort_column, sort_direction](
                             const std::filesystem::directory_entry& a,
                             const std::filesystem::directory_entry& b) {
    if (sort_column == SortColumn::NAME) {
      std::string name_a = a.path().filename().string();
      std::string name_b = b.path().filename().string();
      std::transform(name_a.begin(), name_a.end(), name_a.begin(), ::tolower);
      std::transform(name_b.begin(), name_b.end(), name_b.begin(), ::tolower);
      if (sort_direction == SortDirection::ASCENDING) return name_a < name_b;
      return name_a > name_b;
    } else {
      std::error_code ec_a, ec_b;
      uintmax_t size_a = a.is_directory() ? 0 : a.file_size(ec_a);
      uintmax_t size_b = b.is_directory() ? 0 : b.file_size(ec_b);
      if (sort_direction == SortDirection::ASCENDING) return size_a < size_b;
      return size_a > size_b;
    }
  };

  std::sort(entries.begin(), entries.end(), compare_entries);
}

std::vector<std::filesystem::directory_entry> ReadDirectoryEntries(
    const std::string& directory_path, bool show_hidden_files,
    SortColumn sort_column, SortDirection sort_direction) {
  std::vector<std::filesystem::directory_entry> folders;
  std::vector<std::filesystem::directory_entry> apps;
  std::vector<std::filesystem::directory_entry> files;

  try {
    for (const auto& entry :
         std::filesystem::directory_iterator(directory_path)) {
      std::string name = entry.path().filename().string();
      if (!show_hidden_files && !name.empty() && name[0] == '.') continue;

      if (entry.is_directory()) {
        if (GetExtension(name) == "app") {
          apps.push_back(entry);
        } else {
          folders.push_back(entry);
        }
      } else {
        files.push_back(entry);
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "Error reading directory: " << directory_path << ": "
              << e.what() << std::endl;
  }

  SortDirectoryEntries(folders, sort_column, sort_direction);
  SortDirectoryEntries(apps, sort_column, sort_direction);
  SortDirectoryEntries(files, sort_column, sort_direction);

  std::vector<std::filesystem::directory_entry> result;
  result.reserve(folders.size() + apps.size() + files.size());
  result.insert(result.end(), folders.begin(), folders.end());
  result.insert(result.end(), apps.begin(), apps.end());
  result.insert(result.end(), files.begin(), files.end());
  return result;
}
