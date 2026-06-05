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

#include "database.h"

#include <map>
#include <mutex>
#include <string>

#include "perception/processes.h"
#include "permissions.h"
#include "registry_namespace.h"

using ::perception::MessageId;
using ::perception::NamespaceInfo;
using ::perception::ProcessId;
using ::perception::RegistryCorpus;

namespace {

// The main database containing all namespaces.
std::mutex database_mutex;
std::map<RegistryCorpus,
         std::map<std::string, std::shared_ptr<RegistryNamespace>, std::less<>>>
    database;

// Fast lookup cache for ProcessId -> Namespace.
std::mutex cache_mutex;
std::map<ProcessId, std::shared_ptr<RegistryNamespace>> caller_namespace_cache;
std::map<ProcessId, MessageId> caller_termination_handlers;

// Returns a cached namespace for a ProcessId, if it exists.
std::shared_ptr<RegistryNamespace> GetCachedNamespace(ProcessId caller) {
  std::scoped_lock lock(cache_mutex);
  auto it = caller_namespace_cache.find(caller);
  if (it != caller_namespace_cache.end()) return it->second;
  return nullptr;
}

// Caches a namespace for fast lookup by caller process ID.
void CacheNamespaceForCaller(ProcessId caller,
                             std::shared_ptr<RegistryNamespace> ns) {
  {
    std::scoped_lock lock(cache_mutex);
    caller_namespace_cache[caller] = ns;
  }

  // Listen for when the process terminates so it can be removed from the cache.
  MessageId mid =
      ::perception::NotifyUponProcessTermination(caller, [caller]() {
        std::scoped_lock lock(cache_mutex);
        caller_namespace_cache.erase(caller);
        caller_termination_handlers.erase(caller);
      });

  {
    std::scoped_lock lock(cache_mutex);
    // In case the process terminated in the split second before it registered.
    if (caller_namespace_cache.find(caller) == caller_namespace_cache.end()) {
      ::perception::StopNotifyingUponProcessTermination(mid);
    } else {
      caller_termination_handlers[caller] = mid;
    }
  }
}

// Returns a namespace for the corpus and name. Creates it if it doesn't exist.
std::shared_ptr<RegistryNamespace> GetOrCreateNamespace(RegistryCorpus corpus,
                                                        std::string_view name) {
  std::scoped_lock db_lock(database_mutex);
  auto& ns_map = database[corpus];
  auto it = ns_map.find(name);
  if (it == ns_map.end()) {
    auto ns = std::make_shared<RegistryNamespace>(corpus, name);
    ns_map[std::string(name)] = ns;
    return ns;
  }
  return it->second;
}

// Returns the namespace for a given process.
std::shared_ptr<RegistryNamespace> ResolveCallerNamespace(ProcessId caller) {
  // Check if there is a cached namespace for this process.
  if (auto cached_ns = GetCachedNamespace(caller)) return cached_ns;

  std::string caller_name = GetCachedProcessName(caller);

  // Look up or create the namespace in the database.
  std::shared_ptr<RegistryNamespace> ns =
      GetOrCreateNamespace(RegistryCorpus::APPLICATIONS, caller_name);

  // Cache it so it's remembered for future calls.
  CacheNamespaceForCaller(caller, ns);

  return ns;
}

}  // namespace

std::shared_ptr<RegistryNamespace> ResolveNamespace(
    RegistryCorpus corpus, std::string_view r_namespace, ProcessId caller) {
  if (r_namespace.empty()) return ResolveCallerNamespace(caller);
  return GetOrCreateNamespace(corpus, r_namespace);
}

std::vector<NamespaceInfo> GetNamespaces() {
  std::scoped_lock db_lock(database_mutex);
  std::vector<NamespaceInfo> namespaces;
  for (const auto& corpus_pair : database) {
    for (const auto& ns_pair : corpus_pair.second) {
      NamespaceInfo info;
      info.corpus = corpus_pair.first;
      info.name = ns_pair.first;
      namespaces.push_back(info);
    }
  }
  return namespaces;
}
