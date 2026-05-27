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
#include <string_view>

#include "memory_mapped_file.h"
#include "perception/permissions.h"
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

namespace {

// Returns whether the path is in the application's own directory, which doesn't
// require special permission to access.
bool IsPathWithinApplicationDirectory(std::string_view path,
                                      std::string_view process_name) {
  if (path.empty() || path[0] != '/') {
    return false;
  }

  // Skip the leading '/'
  std::string_view remaining = path.substr(1);

  // Find the first component
  size_t slash_pos = remaining.find('/');
  std::string_view first_comp = (slash_pos == std::string_view::npos)
                                    ? remaining
                                    : remaining.substr(0, slash_pos);

  if (first_comp != "Applications") {
    // If the first component is not "Applications", it could be the
    // mount point. So skip the mount point and look at the next
    // component.
    if (slash_pos == std::string_view::npos) {
      return false;  // Just a mount point, like "/Optical 1"
    }
    remaining = remaining.substr(slash_pos + 1);
    slash_pos = remaining.find('/');
    first_comp = (slash_pos == std::string_view::npos)
                     ? remaining
                     : remaining.substr(0, slash_pos);
  }

  // Now first_comp must be "Applications".
  if (first_comp != "Applications") return false;

  // Skip "Applications"/
  if (slash_pos == std::string_view::npos) {
    return false;  // Just "/Applications"
  }
  remaining = remaining.substr(slash_pos + 1);

  // The next component must match process_name.
  slash_pos = remaining.find('/');
  std::string_view target_comp = (slash_pos == std::string_view::npos)
                                     ? remaining
                                     : remaining.substr(0, slash_pos);

  return target_comp == process_name;
}

// Returns whether a process has permission to read a path.
bool DoesProcessHavePermissionToReadPath(std::string_view path,
                                         ProcessId sender) {
  std::string process_name = GetProcessName(sender);
  if (!process_name.empty() &&
      IsPathWithinApplicationDirectory(path, process_name)) {
    return true;
  }

  return ::perception::DoesProcessHavePermission(
      sender, ::perception::Permission::CanReadAllFiles);
}

}  // namespace

StorageManager::StorageManager() {}

StorageManager::~StorageManager() {}

StatusOr<OpenFileResponse> StorageManager::OpenFile(
    const RequestWithFilePath& request, ProcessId sender) {
  if (!DoesProcessHavePermissionToReadPath(request.path, sender))
    return ::perception::Status::NOT_ALLOWED;

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
  if (!DoesProcessHavePermissionToReadPath(request.path, sender))
    return ::perception::Status::NOT_ALLOWED;
  ASSIGN_OR_RETURN(MemoryMappedFile * mmfile,
                   ::OpenMemoryMappedFile(request.path, sender));
  OpenMemoryMappedFileResponse response;
  response.file = *(::perception::MemoryMappedFile::Server*)mmfile;
  response.file_contents = mmfile->GetBuffer();
  return response;
}

StatusOr<ReadDirectoryResponse> StorageManager::ReadDirectory(
    const ReadDirectoryRequest& request, ProcessId sender) {
  if (!DoesProcessHavePermissionToReadPath(request.path, sender))
    return ::perception::Status::NOT_ALLOWED;
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