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

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Column used for sorting directory entries.
enum class SortColumn { NAME, SIZE };

// Direction used for sorting directory entries.
enum class SortDirection { ASCENDING, DESCENDING };

// Returns the lowercase file extension without a leading dot.
std::string GetExtension(std::string_view name);

// Joins a directory path and an entry name with a single slash separator.
std::string JoinPath(std::string_view dir, std::string_view name);

// Returns a non-colliding path inside `dir` based on `desired_name`, appending
// " (N)" before the extension when needed or when `force_suffix` is true.
std::string GetUniquePath(std::string_view dir, std::string_view desired_name,
                          bool force_suffix = false);

// Normalizes a directory path (stripping trailing slashes except root) and
// resolves top-level symlinks.
std::string NormalizeAndResolveDirectoryPath(std::string_view path);

// Copies a single file in fixed-size chunks, populating `error_message` on
// failure.
bool CopyFileChunked(const std::string& src_path, const std::string& dst_path,
                     std::string& error_message);

// Recursively copies a file or directory tree from `src_path` to `dst_path`,
// populating `error_message` on failure.
bool CopyItemRecursive(const std::string& src_path, const std::string& dst_path,
                       std::string& error_message);

// Recursively deletes a file or directory tree at `target_path`, populating
// `error_message` on failure.
bool DeleteItemRecursive(const std::string& target_path,
                         std::string& error_message);

// Moves or renames a file or directory from `src_path` to `dst_path`,
// populating `error_message` on failure.
bool MoveOrRenameItem(const std::string& src_path, const std::string& dst_path,
                      std::string& error_message);

// Sorts a list of directory entries in place according to `sort_column` and
// `sort_direction`.
void SortDirectoryEntries(
    std::vector<std::filesystem::directory_entry>& entries,
    SortColumn sort_column, SortDirection sort_direction);

// Reads, filters, categorizes (folders, `.app` bundles, files), and sorts the
// entries of `directory_path`.
std::vector<std::filesystem::directory_entry> ReadDirectoryEntries(
    const std::string& directory_path, bool show_hidden_files,
    SortColumn sort_column, SortDirection sort_direction);
