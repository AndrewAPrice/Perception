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

#include "loader.h"

#include <filesystem>
#include <iostream>

#include "elf_loader.h"
#include "perception/processes.h"
// #include "perception/profiling.h"

using ::perception::CreateChildProcess;
using ::perception::DestroyChildProcess;
using ::perception::GetProcessName;
using ::perception::ProcessId;
using ::perception::StartExecutingChildProcess;
using LoaderService = ::permebuf::perception::LoaderService;

namespace {
std::optional<std::string> GetPathToApplication(std::string_view name_sv) {
  if (name_sv.size() == 0) return std::nullopt;

  std::string name = std::string(name_sv);
  if (name[0] == '/') {
    // This is a fully qualified path. Check that it exists.
    if (!std::filesystem::exists(name)) return std::nullopt;
    return name;
  }
  // Check /Applications/ first, since it's the first mount point.
  std::string path_suffix = "/" + name + "/" + name + ".app";
  std::string path = "/Applications" + path_suffix;
  if (std::filesystem::exists(path)) return path;

  // Check each mounted disk.
  for (const auto& root_entry : std::filesystem::directory_iterator("/")) {
    std::string disk_path = std::string(root_entry.path()) + path;
    if (std::filesystem::exists(disk_path)) return disk_path;
  }

  // Can't find the application anywhere.
  return std::nullopt;
}

std::string_view ExtractApplicationNameFromPath(std::string_view path) {
  // Remove the directories from the path name.
  auto slash_index = path.find_last_of('/');
  if (slash_index != std::string_view::npos)
    path = path.substr(slash_index + 1);

  // Remove the extension from the path name.
  auto dot_index = path.find_last_of('.');
  if (dot_index != std::string_view::npos) path = path.substr(0, dot_index);

  return path;
}

}  // namespace

StatusOr<LoaderService::LaunchApplicationResponse>
Loader::HandleLaunchApplication(
    ::perception::ProcessId sender,
    Permebuf<LoaderService::LaunchApplicationRequest> request) {
  // ::perception::EnableProfiling();
  // Extract the path and process name from the request.
  std::string_view name = *request->GetName();
  auto opt_path = GetPathToApplication(name);
  if (!opt_path) {
    std::cout << "Cannot find \"" << name << "\" to load." << std::endl;
    return LoaderService::LaunchApplicationResponse();
  }
  std::string_view path = *opt_path;

  if (path == name) {
    // The provided name was a fully qualified path. So, extract the name from
    // the path.
    name = ExtractApplicationNameFromPath(path);
  }

  // Detecting if something is a driver if the device manager launches it is a
  // temporary solution.
  bool is_driver = GetProcessName(sender) == "Device Manager";
  size_t bitfield = is_driver ? (1 << 0) : 0;

  // Create the child process.

  std::cout << "Loading " << (is_driver ? "driver " : "application ") << name
            << "..." << std::endl;

  ProcessId child_pid;
  if (!CreateChildProcess(name, bitfield, child_pid)) {
    return LoaderService::LaunchApplicationResponse();
  }

  // Load the ELF binary.
  auto status_or_entry_address = LoadElfAndGetEntryAddress(child_pid, path);
  if (!status_or_entry_address.Ok()) {
    std::cout << "Error loading " << path << std::endl;
    DestroyChildProcess(child_pid);
    return LoaderService::LaunchApplicationResponse();
  }

  // Start launching.
  StartExecutingChildProcess(child_pid, *status_or_entry_address, /*params=*/0);

  LoaderService::LaunchApplicationResponse response;
  response.SetProcessId(child_pid);
  // ::perception::DisableAndOutputProfiling();
  return response;
}
