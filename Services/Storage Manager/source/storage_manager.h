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

#include "perception/storage_manager.h"

class StorageManager : public ::perception::StorageManager::Server {
 public:
 
  StorageManager();
  virtual ~StorageManager();

  virtual StatusOr<::perception::OpenFileResponse> OpenFile(
      const ::perception::RequestWithFilePath& request,
      ::perception::ProcessId sender) override;

  virtual StatusOr<::perception::OpenMemoryMappedFileResponse>
  OpenMemoryMappedFile(const ::perception::RequestWithFilePath& request,
                       ::perception::ProcessId sender) override;

  virtual StatusOr<::perception::ReadDirectoryResponse> ReadDirectory(
      const ::perception::ReadDirectoryRequest& request) override;

  virtual StatusOr<::perception::CheckPermissionsResponse> CheckPermissions(
      const ::perception::RequestWithFilePath& request) override;

  virtual StatusOr<::perception::FileStatistics> GetFileStatistics(
      const ::perception::RequestWithFilePath& request) override;
};
