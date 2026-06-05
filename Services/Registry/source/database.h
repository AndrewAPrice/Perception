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
#include <string_view>
#include <vector>

#include "registry_namespace.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/status.h"

// Initialize the database (clears and sets up state).
void InitializeDatabase();

// Resolves a namespace. If r_namespace is empty, it uses the caller's process name.
// Returns a std::shared_ptr to the RegistryNamespace.
// Also keeps a cached map of ProcessId -> Namespace class.
std::shared_ptr<RegistryNamespace> ResolveNamespace(
    ::perception::RegistryCorpus corpus, std::string_view r_namespace,
    ::perception::ProcessId caller);

// Get all namespaces.
std::vector<::perception::NamespaceInfo> GetNamespaces();
