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

#include <map>
#include <mutex>
#include <optional>

#include "perception/processes.h"
#include "perception/serialization/serializer.h"
#include "perception/services.h"

namespace perception {
namespace {

std::mutex g_permission_cache_mutex;

// Map of process -> permission -> whether the process has the permission.
std::map<ProcessId, std::map<Permission, bool>> g_permission_cache;

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
    return *cached;
  }

  // Call the permissions manager.
  DoesProcessHavePermissionRequest request;
  request.process = process;
  request.permission = permission;

  auto status_or_response =
      GetService<PermissionsManager>().DoesProcessHavePermission(request);
  if (!status_or_response.Ok()) return false;

  bool has_permission = status_or_response->has_permission;
  CachePermission(process, permission, has_permission);
  return has_permission;
}

void DoesProcessHavePermission(
    ProcessId process, Permission permission,
    std::function<void(bool)> on_process_has_permission) {
  if (!on_process_has_permission) return;

  if (auto cached = GetCachedPermission(process, permission)) {
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
          on_process_has_permission(false);
          return;
        }

        bool has_permission = status_or_response->has_permission;
        CachePermission(process, permission, has_permission);
        on_process_has_permission(has_permission);
      });
}

}  // namespace perception
