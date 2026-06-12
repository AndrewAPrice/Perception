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

#include "default_permissions.h"

#include <optional>
#include <string_view>

#include "perception/permissions.h"
#include "perception/registry.h"
#include "permission_verbalization.h"
#include "permissions_cache.h"

using perception::Permission;

void PopulateInitialPermissions() {
  // Initial permissions that are needed before permissions can be fetched from
  // the registry.

  // Loader can read all files and launch programs
  SetCachedProcessNamePermission("Loader", Permission::CanReadAllFiles, true);
  SetCachedProcessNamePermission("Loader", Permission::CanLaunchPrograms, true);

  // Device Manager can launch programs.
  SetCachedProcessNamePermission("Device Manager",
                                 Permission::CanLaunchPrograms, true);

  SetCachedProcessNamePermission("Registry", Permission::CanReadAllFiles, true);
}

void LoadPermissionsFromRegistry() {
  auto val_or = perception::GetRegistryValue("granted_permissions");
  if (val_or.Ok()) {
    const auto* array_val = val_or->ArrayValue();
    if (array_val) {
      for (const auto& entry_val : *array_val) {
        const auto* entry = entry_val.ArrayValue();
        if (entry && entry->size() >= 3) {
          auto process_name_opt = (*entry)[0].StringValue();
          auto perm_str_opt = (*entry)[1].StringValue();
          auto allowed_opt = (*entry)[2].BoolValue();
          if (process_name_opt && perm_str_opt && allowed_opt) {
            if (auto perm = ParsePermissionKey(*perm_str_opt)) {
              SetCachedProcessNamePermission(std::string(*process_name_opt),
                                             *perm, *allowed_opt);
            }
          }
        }
      }
    }
  }
}

void InitializeRegistryPermissions() {
  perception::Defer([]() {
    LoadPermissionsFromRegistry();

    // Register a listener for changes to this key
    (void)::perception::RegisterRegistryListener(
        perception::RegistryCorpus::APPLICATIONS, "Permissions Manager",
        "granted_permissions", []() { LoadPermissionsFromRegistry(); });
  });
}
