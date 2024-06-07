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

using ::perception::GetProcessName;
using ::perception::ProcessId;
using ::permebuf::perception::DirectoryEntry;
using ::permebuf::perception::DirectoryEntryType;
using SM = ::permebuf::perception::StorageManager;

// #define DEBUG

StorageManager::StorageManager() {}

StorageManager::~StorageManager() {}

StatusOr<SM::OpenFileResponse> StorageManager::HandleOpenFile(
    ::perception::ProcessId sender, Permebuf<SM::OpenFileRequest> request) {
#ifdef DEBUG
  std::cout << GetProcessName(sender) << " wants to open "
            << *request->GetPath() << std::endl;
#endif
  size_t size_in_bytes = 0;
  size_t optimal_operation_size = 0;
  ASSIGN_OR_RETURN(auto* file, OpenFile(*request->GetPath(), size_in_bytes,
                                        optimal_operation_size, sender));

  SM::OpenFileResponse response;
  response.SetFile(*(::permebuf::perception::File::Server*)file);
  response.SetSizeInBytes(size_in_bytes);
  response.SetOptimalOperationSize(optimal_operation_size);
  return response;
}

StatusOr<SM::OpenMemoryMappedFileResponse>
StorageManager::HandleOpenMemoryMappedFile(
    ::perception::ProcessId sender,
    Permebuf<SM::OpenMemoryMappedFileRequest> request) {
#ifdef DEBUG
  std::cout << GetProcessName(sender) << " wants to memory map "
            << *request->GetPath() << std::endl;
#endif
  ASSIGN_OR_RETURN(MemoryMappedFile * mmfile,
                   OpenMemoryMappedFile(*request->GetPath(), sender));
  SM::OpenMemoryMappedFileResponse response;
  response.SetFile(*(::permebuf::perception::MemoryMappedFile::Server*)mmfile);
  response.SetFileContents(mmfile->GetBuffer());
  return response;
}

StatusOr<Permebuf<SM::ReadDirectoryResponse>>
StorageManager::HandleReadDirectory(
    ::perception::ProcessId sender,
    Permebuf<SM::ReadDirectoryRequest> request) {
#ifdef DEBUG
  std::cout << GetProcessName(sender) << " wants to iterate through "
            << *request->GetPath() << std::endl;
#endif
  Permebuf<SM::ReadDirectoryResponse> response;

  PermebufListOf<DirectoryEntry> last_directory_entry;

  bool no_more_entries = ForEachEntryInDirectory(
      *request->GetPath(), request->GetFirstIndex(),
      request->GetMaximumNumberOfEntries(),
      [&](std::string_view name, DirectoryEntryType type, size_t size) {
        auto directory_entry = response.AllocateMessage<DirectoryEntry>();
        directory_entry.SetName(name);
        directory_entry.SetType(type);
        directory_entry.SetSizeInBytes(size);

        if (last_directory_entry.IsValid()) {
          last_directory_entry = last_directory_entry.InsertAfter();
        } else {
          last_directory_entry = response->MutableEntries();
        }
        last_directory_entry.Set(directory_entry);
      });
  response->SetHasMoreEntries(!no_more_entries);
  return response;
}

StatusOr<SM::CheckPermissionsResponse> StorageManager::HandleCheckPermissions(
    ::perception::ProcessId sender,
    Permebuf<SM::CheckPermissionsRequest> request) {
#ifdef DEBUG
  std::cout << GetProcessName(sender) << " wants to check "
            << *request->GetPath() << std::endl;
#endif
  SM::CheckPermissionsResponse response;
  bool file_exists, can_read, can_write, can_execute;

  RETURN_ON_ERROR(CheckFilePermissions(*request->GetPath(), file_exists,
                                       can_read, can_write, can_execute));

  response.SetFileExists(file_exists);
  response.SetCanRead(can_read);
  response.SetCanWrite(can_write);
  response.SetCanExecute(can_execute);

  return response;
}

StatusOr<SM::GetFileStatisticsResponse> StorageManager::HandleGetFileStatistics(
    ::perception::ProcessId sender,
    Permebuf<SM::GetFileStatisticsRequest> request) {
#ifdef DEBUG
  std::cout << GetProcessName(sender) << " wants to get stats about "
            << *request->GetPath() << std::endl;
#endif
  return GetFileStatistics(*request->GetPath());
}