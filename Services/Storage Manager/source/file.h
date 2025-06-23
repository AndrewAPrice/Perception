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

#include "perception/file.h"
#include "perception/storage_manager.h"

// A wrapper that makes the protected functions public, so we can call them
// directly.
class File : public ::perception::File::Server {
 public:
  virtual ::perception::Status Close(
      ::perception::ProcessId sender) override = 0;

  virtual ::perception::Status Read(
      const ::perception::ReadFileRequest& request,
      ::perception::ProcessId sender) override = 0;

  virtual ::perception::Status
  GrantStorageDevicePermissionToAllocateSharedMemoryPages(
      const ::perception::
          GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest&
              request,
      ::perception::ProcessId sender) override = 0;
};
