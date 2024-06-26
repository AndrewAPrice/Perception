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

#pragma once

#include "permebuf/Libraries/perception/storage_manager.permebuf.h"

class StorageManager : public ::permebuf::perception::StorageManager::Server {
 public:
  typedef ::permebuf::perception::StorageManager SM;

  StorageManager();
  virtual ~StorageManager();

  virtual StatusOr<SM::OpenFileResponse> HandleOpenFile(
      ::perception::ProcessId sender,
      Permebuf<SM::OpenFileRequest> request) override;

  virtual StatusOr<SM::OpenMemoryMappedFileResponse> HandleOpenMemoryMappedFile(
      ::perception::ProcessId sender,
      Permebuf<SM::OpenMemoryMappedFileRequest> request) override;

  virtual StatusOr<Permebuf<SM::ReadDirectoryResponse>> HandleReadDirectory(
      ::perception::ProcessId sender,
      Permebuf<SM::ReadDirectoryRequest> request) override;

  virtual StatusOr<SM::CheckPermissionsResponse> HandleCheckPermissions(
      ::perception::ProcessId sender,
      Permebuf<SM::CheckPermissionsRequest> request) override;

  virtual StatusOr<SM::GetFileStatisticsResponse> HandleGetFileStatistics(
      ::perception::ProcessId sender,
      Permebuf<SM::GetFileStatisticsRequest> request) override;
};
