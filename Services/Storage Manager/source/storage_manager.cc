// Copyright 2020 Google LLC
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

#include "storage_manager.h"

#include <iostream>

#include "memory_mapped_file.h"
#include "perception/processes.h"
#include "virtual_file_system.h"

using ::perception::CheckPermissionsResponse;
using ::perception::DirectoryEntry;
using ::perception::FileStatistics;
using ::perception::GetProcessName;
using ::perception::OpenFileResponse;
using ::perception::OpenMemoryMappedFileResponse;
using ::perception::ProcessId;
using ::perception::ReadDirectoryRequest;
using ::perception::ReadDirectoryResponse;
using ::perception::RequestWithFilePath;

StorageManager::StorageManager() {}

StorageManager::~StorageManager() {}

StatusOr<OpenFileResponse> StorageManager::OpenFile(
    const RequestWithFilePath& request, ProcessId sender) {
  size_t size_in_bytes = 0;
  size_t optimal_operation_size = 0;
  ASSIGN_OR_RETURN(auto* file, ::OpenFile(request.path, size_in_bytes,
                                          optimal_operation_size, sender));

  OpenFileResponse response;
  response.file = *(::perception::File::Server*)file;
  response.size_in_bytes = size_in_bytes;
  response.optimal_operation_size = optimal_operation_size;
  return response;
}

StatusOr<OpenMemoryMappedFileResponse> StorageManager::OpenMemoryMappedFile(
    const RequestWithFilePath& request, ::perception::ProcessId sender) {
  ASSIGN_OR_RETURN(MemoryMappedFile * mmfile,
                   ::OpenMemoryMappedFile(request.path, sender));
  OpenMemoryMappedFileResponse response;
  response.file = *(::perception::MemoryMappedFile::Server*)mmfile;
  response.file_contents = mmfile->GetBuffer();
  return response;
}

StatusOr<ReadDirectoryResponse> StorageManager::ReadDirectory(
    const ReadDirectoryRequest& request) {
  ReadDirectoryResponse response;

  response.has_more_entries = !ForEachEntryInDirectory(
      request.path, request.first_index, request.maximum_number_of_entries,
      [&](std::string_view name, DirectoryEntry::Type type, size_t size) {
        auto& directory_entry = response.entries.emplace_back();
        directory_entry.name = name;
        directory_entry.type = static_cast<DirectoryEntry::Type>(type);
        directory_entry.size_in_bytes = size;
      });
  return response;
}

StatusOr<CheckPermissionsResponse> StorageManager::CheckPermissions(
    const RequestWithFilePath& request) {
  CheckPermissionsResponse response;
  RETURN_ON_ERROR(CheckFilePermissions(request.path, response.exists,
                                       response.can_read, response.can_write,
                                       response.can_execute));
  return response;
}

StatusOr<FileStatistics> StorageManager::GetFileStatistics(
    const RequestWithFilePath& request) {
  return ::GetFileStatistics(request.path);
}