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

#include "permissions.h"

#include <map>
#include <mutex>

#include "perception/messages.h"
#include "perception/permissions.h"

using ::perception::MessageId;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::RegistryCorpus;

namespace {

std::mutex name_cache_mutex;
std::map<ProcessId, std::string> process_name_cache;
std::map<ProcessId, MessageId> termination_handlers;

}  // namespace

std::string GetCachedProcessName(ProcessId pid) {
  {
    std::scoped_lock lock(name_cache_mutex);
    auto it = process_name_cache.find(pid);
    if (it != process_name_cache.end()) return it->second;
  }

  std::string name = ::perception::GetProcessName(pid);

  {
    std::scoped_lock lock(name_cache_mutex);
    process_name_cache[pid] = name;
  }

  MessageId mid = ::perception::NotifyUponProcessTermination(pid, [pid]() {
    std::scoped_lock lock(name_cache_mutex);
    process_name_cache.erase(pid);
    termination_handlers.erase(pid);
  });

  {
    std::scoped_lock lock(name_cache_mutex);
    if (process_name_cache.find(pid) == process_name_cache.end()) {
      ::perception::StopNotifyingUponProcessTermination(mid);
    } else {
      termination_handlers[pid] = mid;
    }
  }

  return name;
}

bool CanReadNamespace(const std::shared_ptr<RegistryNamespace>& ns,
                      std::string_view requested_namespace, ProcessId caller) {
  if (requested_namespace.empty() &&
      ns->GetCorpus() == RegistryCorpus::APPLICATIONS)
    return true;  // Fast path: Caller's own applications namespace

  if (ns->GetCorpus() == RegistryCorpus::LIBRARIES)
    return true;  // Libraries are read-only public.

  std::string caller_name = GetCachedProcessName(caller);
  if (caller_name == ns->GetName())
    return true;  // Application can read its own namespace

  return ::perception::DoesProcessHavePermission(
      caller, Permission::CanViewAndModifyEntireRegistry);
}

bool CanWriteNamespace(const std::shared_ptr<RegistryNamespace>& ns,
                       std::string_view requested_namespace, ProcessId caller) {
  if (requested_namespace.empty() &&
      ns->GetCorpus() == RegistryCorpus::APPLICATIONS)
    return true;  // Caller's own applications namespace.

  std::string caller_name = GetCachedProcessName(caller);
  if (ns->GetCorpus() == RegistryCorpus::APPLICATIONS &&
      caller_name == ns->GetName())
    return true;  // Application can write its own namespace

  return ::perception::DoesProcessHavePermission(
      caller, Permission::CanViewAndModifyEntireRegistry);
}
