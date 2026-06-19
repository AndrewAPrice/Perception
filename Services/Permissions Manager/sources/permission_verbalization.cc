// Copyright 2026 Google LLC
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

#include "permission_verbalization.h"

using perception::Permission;

namespace {

// Returns a static string_view verbalization of a permission. Returns nullopt
// if a permission isn't handled.
std::optional<std::string_view> GetPermissionVerbalization(
    Permission permission) {
  switch (permission) {
    case Permission::CanReadAllFiles:
      return "read all files";
    case Permission::CanLaunchPrograms:
      return "launch programs";
    case Permission::CanViewAndModifyEntireRegistry:
      return "view and modify the entire registry";
    case Permission::CanUseNetworkDevice:
      return "use the network device directly";
    case Permission::CanContinueRunningAfterWindowsClose:
      return "continue running after all windows are closed";
    default:
      return std::nullopt;
  }
}
}  // namespace

std::optional<std::string> GetPermissionRequestVerbalization(
    std::string_view process_name, perception::Permission permission) {
  auto verbalized_permission = GetPermissionVerbalization(permission);
  if (!verbalized_permission.has_value()) {
    return std::nullopt;
  }

  return std::string(process_name) + " is requesting permission to " +
         std::string(*verbalized_permission) + ".";
}

std::optional<std::string_view> GetPermissionKey(Permission permission) {
  switch (permission) {
    case Permission::CanReadAllFiles:
      return "CanReadAllFiles";
    case Permission::CanLaunchPrograms:
      return "CanLaunchPrograms";
    case Permission::CanViewAndModifyEntireRegistry:
      return "CanViewAndModifyEntireRegistry";
    case Permission::CanUseNetworkDevice:
      return "CanUseNetworkDevice";
    case Permission::CanContinueRunningAfterWindowsClose:
      return "CanContinueRunningAfterWindowsClose";
    default:
      return std::nullopt;
  }
}

std::optional<Permission> ParsePermissionKey(std::string_view key) {
  if (key == "CanReadAllFiles") return Permission::CanReadAllFiles;
  if (key == "CanLaunchPrograms") return Permission::CanLaunchPrograms;
  if (key == "CanViewAndModifyEntireRegistry")
    return Permission::CanViewAndModifyEntireRegistry;
  if (key == "CanUseNetworkDevice") return Permission::CanUseNetworkDevice;
  if (key == "CanContinueRunningAfterWindowsClose")
    return Permission::CanContinueRunningAfterWindowsClose;
  return std::nullopt;
}
