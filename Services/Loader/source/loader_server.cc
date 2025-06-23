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

#include <iostream>

#include "loader.h"

using ::perception::ProcessId;

StatusOr<::perception::LoadApplicationResponse> LoaderServer::LaunchApplication(
    const ::perception::LoadApplicationRequest& request,
  ProcessId sender) {
  std::cout << "Requested to load " << request.name << std::endl;
  ASSIGN_OR_RETURN(auto child_pid, LoadProgram(sender, request.name));

  ::perception::LoadApplicationResponse response;
  response.process = child_pid;
  return response;
}
