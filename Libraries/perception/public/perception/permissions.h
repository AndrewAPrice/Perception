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
// #define PERCEPTION
#pragma once

#include <memory>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {
class Serializer;
}

// Permissions that processes can have.
enum class Permission { CanReadAllFiles, CanLaunchPrograms };

class DoesProcessHavePermissionRequest : public serialization::Serializable {
 public:
  ProcessId process;
  Permission permission;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class DoesProcessHavePermissionResponse : public serialization::Serializable {
 public:
  bool has_permission;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// Synchronously checks if an process has a permission.
bool DoesProcessHavePermission(ProcessId process, Permission permission);

// Asynchronously checks if an process has a permission.
void DoesProcessHavePermission(
    ProcessId process, Permission permission,
    std::function<void(bool)> on_process_has_permission);

#define METHOD_LIST(X)                                               \
  X(1, DoesProcessHavePermission, DoesProcessHavePermissionResponse, \
    DoesProcessHavePermissionRequest)

DEFINE_PERCEPTION_SERVICE(PermissionsManager, "perception.PermissionsManager",
                          METHOD_LIST)
#undef METHOD_LIST

}  // namespace perception