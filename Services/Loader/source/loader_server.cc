// Copyright 2024 Google LLC
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

#include "loader_server.h"

#include "loader.h"

using ::perception::ProcessId;
using LoaderService = ::permebuf::perception::LoaderService;

StatusOr<LoaderService::LaunchApplicationResponse>
LoaderServer::HandleLaunchApplication(
    ProcessId sender,
    Permebuf<LoaderService::LaunchApplicationRequest> request) {
  std::cout << "Requested to load " << *request->GetName() << std::endl;
  ASSIGN_OR_RETURN(auto child_pid, LoadProgram(sender, *request->GetName()));

  LoaderService::LaunchApplicationResponse response;
  response.SetProcessId(child_pid);
  return response;
}
