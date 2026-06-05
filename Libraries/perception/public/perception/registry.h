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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perception/processes.h"
#include "perception/registry_service.h"
#include "status.h"

// The registry is a service that holds key/value pairs.
// Processes have read/write access to their own namespace and read access to
// library namespaces. The registry can also be edited in the Settings
// application.
namespace perception {

// Returns a value from the registry.
StatusOr<serialization::Value> GetRegistryValue(std::string_view key);

// Returns a value from the registry, in a specific corpus and namespace.
StatusOr<serialization::Value> GetRegistryValue(RegistryCorpus corpus,
                                                std::string_view r_namespace,
                                                std::string_view key);

// Sets a value in the registry.
void SetRegistryValue(std::string_view key, const serialization::Value& value);

// Sets a value in the registry, in a specific corpus and namespace.
void SetRegistryValue(RegistryCorpus corpus, std::string_view r_namespace,
                      std::string_view key, const serialization::Value& value);

// Deletes a value from the registry.
void DeleteRegistryValue(std::string_view key);

// Deletes a value from the registry in a specific corpus and namespace.
void DeleteRegistryValue(RegistryCorpus corpus, std::string_view r_namespace,
                         std::string_view key);

// Returns a list of registry keys.
StatusOr<std::vector<std::string>> GetRegistryKeys();

// Returns a list of registry keys in a specific corpus and namespace.
StatusOr<std::vector<std::string>> GetRegistryKeys(
    RegistryCorpus corpus, std::string_view r_namespace);

// Returns the list of namespaces in the registry.
StatusOr<std::vector<NamespaceInfo>> GetNamespacesInRegistry();

using RegistryListenerToken = uint64;

// Sets up a callback for when a key is updated.
StatusOr<RegistryListenerToken> RegisterRegistryListener(
    std::string_view key, std::function<void()> callback);

// Sets up a callback for when a key is updated in a specific corpus and
// namespace.
StatusOr<RegistryListenerToken> RegisterRegistryListener(
    RegistryCorpus corpus, std::string_view r_namespace, std::string_view key,
    std::function<void()> callback);

// Unregisters a listener.
Status UnregisterRegistryListener(RegistryListenerToken token);

}  // namespace perception
