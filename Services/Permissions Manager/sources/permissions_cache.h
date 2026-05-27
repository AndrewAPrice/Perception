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

#pragma once

#include <optional>
#include <string_view>

#include "perception/permissions.h"
#include "perception/processes.h"

namespace perception {

// Checks if a PID has a specific permission cached.
// Returns std::nullopt if not in cache.
std::optional<bool> GetCachedPidPermission(ProcessId pid,
                                           Permission permission);

// Sets a cached permission for a specific PID.
void SetCachedPidPermission(ProcessId pid, Permission permission,
                            bool has_permission);

// Checks if a process name has a specific permission cached.
// Returns std::nullopt if not in cache.
std::optional<bool> GetCachedProcessNamePermission(
    std::string_view process_name, Permission permission);

// Sets a cached permission for a process name.
void SetCachedProcessNamePermission(std::string_view process_name,
                                    Permission permission, bool has_permission);

}  // namespace perception
