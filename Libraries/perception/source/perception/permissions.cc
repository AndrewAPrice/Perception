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

#include "perception/permissions.h"

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "perception/processes.h"
#include "perception/serialization/serializer.h"
#include "perception/services.h"
#include "perception/time.h"
namespace perception {

namespace {

void (*g_permission_denied_toast_handler)(ProcessId, Permission) = nullptr;

}  // namespace

void SetPermissionDeniedToastHandler(
    void (*handler)(ProcessId process, Permission permission)) {
  g_permission_denied_toast_handler = handler;
}

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
    case Permission::CanPlayAudio:
      return "play audio";
    case Permission::CanAdjustVolume:
      return "adjust global volume";
    default:
      return std::nullopt;
  }
}

namespace {

std::mutex g_permission_cache_mutex;

// Map of process -> permission -> whether the process has the permission.
std::map<ProcessId, std::map<Permission, bool>> g_permission_cache;

void PostPermissionDeniedToast(ProcessId process, Permission permission) {
  std::string process_name = GetProcessName(process);
  if (process_name.empty()) process_name = "Process " + std::to_string(process);

  static std::mutex toast_mutex;
  static std::map<std::pair<ProcessId, Permission>, std::chrono::microseconds>
      last_toast_times;

  auto now = GetTimeSinceKernelStarted();
  {
    std::scoped_lock lock(toast_mutex);
    auto key = std::make_pair(process, permission);
    auto it = last_toast_times.find(key);
    if (it != last_toast_times.end()) {
      if (now - it->second < std::chrono::seconds(3)) return;
    }
    last_toast_times[key] = now;
  }

  auto verbalization = GetPermissionVerbalization(permission);
  std::string text;
  if (verbalization) {
    text = "\"" + process_name + "\" was denied permission to " +
           std::string(*verbalization) + ".";
  } else {
    text = "\"" + process_name + "\" was denied permission.";
  }

  if (g_permission_denied_toast_handler) {
    g_permission_denied_toast_handler(process, permission);
  }
}

// Returns the permission if it's cached.
std::optional<bool> GetCachedPermission(ProcessId process,
                                        Permission permission) {
  std::scoped_lock lock(g_permission_cache_mutex);
  auto proc_it = g_permission_cache.find(process);
  if (proc_it != g_permission_cache.end()) {
    auto perm_it = proc_it->second.find(permission);
    if (perm_it != proc_it->second.end()) {
      return perm_it->second;
    }
  }
  return std::nullopt;
}

// Caches the permission.
void CachePermission(ProcessId process, Permission permission,
                     bool has_permission) {
  std::scoped_lock lock(g_permission_cache_mutex);
  auto proc_it = g_permission_cache.find(process);
  if (proc_it == g_permission_cache.end()) {
    // First time caching a permission for this process, so monitor when it
    // terminates so the cache can be cleared.
    NotifyUponProcessTermination(process, [process]() {
      std::scoped_lock lock(g_permission_cache_mutex);
      g_permission_cache.erase(process);
    });
  }
  g_permission_cache[process][permission] = has_permission;
}

}  // namespace

void DoesProcessHavePermissionRequest::Serialize(
    serialization::Serializer& serializer) {
  serializer.Integer("Process", process);
  serializer.Integer("Permission", permission);
}

void DoesProcessHavePermissionResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.Integer("HasPermission", has_permission);
}

bool DoesProcessHavePermission(ProcessId process, Permission permission) {
  if (auto cached = GetCachedPermission(process, permission)) {
    if (!*cached) {
      PostPermissionDeniedToast(process, permission);
    }
    return *cached;
  }

  auto permissions_client = GetService<PermissionsManager>();

  // Call the permissions manager.
  DoesProcessHavePermissionRequest request;
  request.process = process;
  request.permission = permission;

  auto status_or_response =
      permissions_client.DoesProcessHavePermission(request);
  if (!status_or_response.Ok()) {
    PostPermissionDeniedToast(process, permission);
    return false;
  }

  bool has_permission = status_or_response->has_permission;
  CachePermission(process, permission, has_permission);
  if (!has_permission) {
    PostPermissionDeniedToast(process, permission);
  }
  return has_permission;
}

void DoesProcessHavePermission(
    ProcessId process, Permission permission,
    std::function<void(bool)> on_process_has_permission) {
  if (!on_process_has_permission) return;

  if (auto cached = GetCachedPermission(process, permission)) {
    if (!*cached) {
      PostPermissionDeniedToast(process, permission);
    }
    on_process_has_permission(*cached);
    return;
  }

  // Call the permissions manager.
  DoesProcessHavePermissionRequest request;
  request.process = process;
  request.permission = permission;

  (void)GetService<PermissionsManager>().DoesProcessHavePermission(
      request,
      [on_process_has_permission, process, permission](
          StatusOr<DoesProcessHavePermissionResponse> status_or_response) {
        if (!status_or_response) {
          PostPermissionDeniedToast(process, permission);
          on_process_has_permission(false);
          return;
        }

        bool has_permission = status_or_response->has_permission;
        CachePermission(process, permission, has_permission);
        if (!has_permission) {
          PostPermissionDeniedToast(process, permission);
        }
        on_process_has_permission(has_permission);
      });
}

}  // namespace perception
