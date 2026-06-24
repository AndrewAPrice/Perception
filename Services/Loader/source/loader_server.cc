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

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>

#include "extension_registry.h"
#include "file.h"
#include "loader.h"
#include "multiboot.h"
#include "perception/permissions.h"
#include "perception/processes.h"
#include "perception/scheduler.h"

using ::perception::ProcessId;

namespace {

std::shared_ptr<::perception::SharedMemory> multiboot_registry_file_keep_alive;

}  // namespace

StatusOr<::perception::LoadApplicationResponse> LoaderServer::LaunchApplication(
    const ::perception::LoadApplicationRequest& request, ProcessId sender) {
  if (!is_cache_populated) {
    fibers_waiting_for_cache.push_back(
        ::perception::GetCurrentlyExecutingFiber());
    while (!is_cache_populated) {
      ::perception::Sleep();
    }
  }

  if (!::perception::DoesProcessHavePermission(
          sender, ::perception::Permission::CanLaunchPrograms)) {
    return Status::NOT_ALLOWED;
  }
  std::string name_to_load = request.name;
  std::vector<std::string> arguments = request.arguments;

  int iterations = 0;
  while (true) {
    if (iterations++ > 10) return Status::INVALID_ARGUMENT;

    if (!name_to_load.empty() && name_to_load[0] == '/') {
      // It's a file or directory. Let's first check if it's a directory to
      // append '/' if needed.
      std::error_code ec;
      if (std::filesystem::is_directory(name_to_load, ec) &&
          name_to_load.back() != '/') {
        if (name_to_load.size() < 4 ||
            name_to_load.substr(name_to_load.size() - 4) != ".app") {
          name_to_load += "/";
        }
      }

      auto opt_command = GetCommandForPath(name_to_load);
      if (!opt_command.has_value()) {
        std::cout << "Loader error: Cannot handle file type of \""
                  << name_to_load << "\"." << std::endl;
        return Status::INVALID_ARGUMENT;
      }

      const ExtensionCommand& command = *opt_command;
      if (command.action == ExtensionAction::EXECUTE) {
        break;
      } else {
        arguments.insert(arguments.begin(), name_to_load);
        name_to_load = command.application;
      }
    } else {
      // Assume it's an application name and resolve to its full path.
      auto opt_path = GetPathToFile(name_to_load);
      if (!opt_path.has_value()) {
        std::cout << "Loader error: Cannot find program \"" << name_to_load
                  << "\"." << std::endl;
        return Status::INVALID_ARGUMENT;
      }
      name_to_load = *opt_path;
    }
  }

  ASSIGN_OR_RETURN(ProcessId child_pid,
                   LoadProgram(sender, name_to_load, arguments));

  ::perception::LoadApplicationResponse response;
  response.process = child_pid;
  return response;
}

StatusOr<::perception::GetMultibootRegistryFileResponse>
LoaderServer::GetMultibootRegistryFile(
    const ::perception::GetMultibootRegistryFileRequest& request,
    ::perception::ProcessId sender) {
  if (::perception::GetProcessName(sender) != "Registry") {
    return Status::NOT_ALLOWED;
  }

  auto module = GetMultibootModule("registry.json");
  if (!module) return Status::FILE_NOT_FOUND;

  auto shared_memory =
      ::perception::SharedMemory::FromSize(module->data.Length(), 0);
  if (!shared_memory) return Status::OUT_OF_MEMORY;

  std::memcpy(**shared_memory, *module->data, module->data.Length());

  multiboot_registry_file_keep_alive = shared_memory;

  ::perception::GetMultibootRegistryFileResponse response;
  response.registry_file_id = shared_memory->GetId();
  return response;
}
