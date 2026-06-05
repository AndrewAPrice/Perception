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

#include <memory>

#include "perception/processes.h"
#include "registry_namespace.h"

// Checks if the caller has permissions to read a namespace.
bool CanReadNamespace(const std::shared_ptr<RegistryNamespace>& ns,
                      std::string_view requested_namespace,
                      ::perception::ProcessId caller);

// Checks if the caller has permissions to write to a namespace.
bool CanWriteNamespace(const std::shared_ptr<RegistryNamespace>& ns,
                       std::string_view requested_namespace,
                       ::perception::ProcessId caller);

// Returns the process name from a PID, caching the result.
std::string GetCachedProcessName(::perception::ProcessId pid);
