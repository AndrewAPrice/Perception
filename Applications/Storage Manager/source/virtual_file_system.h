// Copyright 2021 Google LLC
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
#include <string_view>

#include "file_systems/file_system.h"
#include "permebuf/Libraries/perception/storage_manager.permebuf.h"

class File;
class MemoryMappedFile;

void MountFileSystem(std::unique_ptr<file_systems::FileSystem> file_system);

StatusOr<File*> OpenFile(std::string_view path, size_t& size_in_bytes,
                         ::perception::ProcessId sender);

StatusOr<MemoryMappedFile*> OpenMemoryMappedFile(
    std::string_view path, ::perception::ProcessId sender);

::perception::Status CheckFilePermissions(std::string_view path,
                                          bool& file_exists, bool& can_read,
                                          bool& can_write, bool& can_execute);

void CloseFile(::perception::ProcessId sender, File* file);

void CloseMemoryMappedFile(::perception::ProcessId sender,
                           MemoryMappedFile* file);

bool ForEachEntryInDirectory(
    std::string_view directory, int offset, int count,
    const std::function<void(std::string_view,
                             ::permebuf::perception::DirectoryEntryType,
                             size_t)>& on_each_entry);

StatusOr<::permebuf::perception::StorageManager::GetFileStatisticsResponse>
GetFileStatistics(std::string_view path);