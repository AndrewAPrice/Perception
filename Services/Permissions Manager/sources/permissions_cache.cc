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

#include "permissions_cache.h"

#include <map>
#include <mutex>
#include <string>

namespace perception {
namespace {

// Mutex to protect the cache maps.
std::mutex g_cache_mutex;

// A map of process IDs and their permissions.
std::map<ProcessId, std::map<Permission, bool>> g_pid_permissions;

// A map of process names and their permissions.
std::map<std::string, std::map<Permission, bool>, std::less<>>
    g_process_name_permissions;

void ClearCachedPidPermissions(ProcessId pid) {
  std::scoped_lock lock(g_cache_mutex);
  g_pid_permissions.erase(pid);
}

}  // namespace

std::optional<bool> GetCachedPidPermission(ProcessId pid,
                                           Permission permission) {
  std::scoped_lock lock(g_cache_mutex);
  auto pid_it = g_pid_permissions.find(pid);
  if (pid_it != g_pid_permissions.end()) {
    auto perm_it = pid_it->second.find(permission);
    if (perm_it != pid_it->second.end()) {
      return perm_it->second;
    }
  }
  return std::nullopt;
}

void SetCachedPidPermission(ProcessId pid, Permission permission,
                            bool has_permission) {
  bool is_first_permission = false;
  {
    std::scoped_lock lock(g_cache_mutex);
    is_first_permission =
        g_pid_permissions.find(pid) == g_pid_permissions.end();
    g_pid_permissions[pid][permission] = has_permission;
  }

  if (is_first_permission) {
    NotifyUponProcessTermination(pid,
                                 [pid]() { ClearCachedPidPermissions(pid); });
  }
}

std::optional<bool> GetCachedProcessNamePermission(
    std::string_view process_name, Permission permission) {
  std::scoped_lock lock(g_cache_mutex);
  auto name_it = g_process_name_permissions.find(process_name);
  if (name_it != g_process_name_permissions.end()) {
    auto perm_it = name_it->second.find(permission);
    if (perm_it != name_it->second.end()) {
      return perm_it->second;
    }
  }
  return std::nullopt;
}

void SetCachedProcessNamePermission(std::string_view process_name,
                                    Permission permission,
                                    bool has_permission) {
  std::scoped_lock lock(g_cache_mutex);
  g_process_name_permissions[std::string(process_name)][permission] =
      has_permission;
}

}  // namespace perception
