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

#include "registry_server.h"

#include <string>

#include "database.h"
#include "perception/permissions.h"
#include "permissions.h"
#include "registry_value.h"
#include "settings_loader.h"

using ::perception::DeleteRegistryValueRequest;
using ::perception::GetNamespacesResponse;
using ::perception::GetRegistryKeysRequest;
using ::perception::GetRegistryKeysResponse;
using ::perception::GetRegistryValueRequest;
using ::perception::GetRegistryValueResponse;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::RegisterRegistryListenerRequest;
using ::perception::RegistryCorpus;
using ::perception::RegistryKeyValue;
using ::perception::SetRegistryValueRequest;
using ::perception::SetRegistryValuesRequest;
using ::perception::UnregisterRegistryListenerRequest;

namespace {

StatusOr<std::shared_ptr<RegistryNamespace>> ResolveAuthorizedNamespace(
    RegistryCorpus corpus, std::string_view r_namespace, ProcessId sender,
    bool write) {
  auto ns = ResolveNamespace(corpus, r_namespace, sender);
  if (!(write ? CanWriteNamespace(ns, r_namespace, sender)
              : CanReadNamespace(ns, r_namespace, sender)))
    return Status::NOT_ALLOWED;
  return ns;
}

// Writes every value, then notifies listeners of the keys that changed.
Status SetValuesAndNotify(RegistryCorpus corpus, std::string_view r_namespace,
                          const std::vector<RegistryKeyValue>& values,
                          ProcessId sender) {
  ASSIGN_OR_RETURN(auto ns, ResolveAuthorizedNamespace(corpus, r_namespace,
                                                       sender, /*write=*/true));

  bool is_owner = IsNamespaceOwner(ns, r_namespace, sender);
  for (const auto& key_value : values) {
    if (!is_owner && ns->IsReadOnly(key_value.key))
      return Status::NOT_ALLOWED;
  }

  std::vector<std::string_view> changed_keys;
  bool persistent_value_changed = false;
  for (const auto& key_value : values) {
    if (ns->SetValue(key_value.key, key_value.value)) {
      changed_keys.push_back(key_value.key);
      if (!ns->IsReadOnly(key_value.key)) persistent_value_changed = true;
    }
  }
  for (std::string_view key : changed_keys) ns->NotifyListeners(key);
  if (persistent_value_changed) RecordRegistryModification();
  return Status::OK;
}

}  // namespace

StatusOr<GetRegistryValueResponse> RegistryServer::GetRegistryValue(
    const GetRegistryValueRequest& request, ProcessId sender) {
  ASSIGN_OR_RETURN(
      auto ns, ResolveAuthorizedNamespace(request.corpus, request.r_namespace,
                                          sender, /*write=*/false));

  GetRegistryValueResponse response;
  ASSIGN_OR_RETURN(response.value, ns->GetValue(request.key));
  return response;
}

Status RegistryServer::SetRegistryValue(const SetRegistryValueRequest& request,
                                        ProcessId sender) {
  std::vector<RegistryKeyValue> values(1);
  values[0].key = request.key;
  values[0].value = request.value;
  return SetValuesAndNotify(request.corpus, request.r_namespace, values,
                            sender);
}

Status RegistryServer::SetRegistryValues(
    const SetRegistryValuesRequest& request, ProcessId sender) {
  return SetValuesAndNotify(request.corpus, request.r_namespace,
                            request.values, sender);
}

Status RegistryServer::DeleteRegistryValue(
    const DeleteRegistryValueRequest& request, ProcessId sender) {
  ASSIGN_OR_RETURN(
      auto ns, ResolveAuthorizedNamespace(request.corpus, request.r_namespace,
                                          sender, /*write=*/true));

  bool is_read_only = ns->IsReadOnly(request.key);
  if (is_read_only && !IsNamespaceOwner(ns, request.r_namespace, sender))
    return Status::NOT_ALLOWED;

  if (ns->DeleteValue(request.key)) {
    if (!is_read_only) RecordRegistryModification();
  }
  return Status::OK;
}

Status RegistryServer::RegisterRegistryListener(
    const RegisterRegistryListenerRequest& request, ProcessId sender) {
  auto ns = ResolveNamespace(request.corpus, request.r_namespace, sender);
  if (!CanReadNamespace(ns, request.r_namespace, sender))
    return Status::NOT_ALLOWED;

  ns->RegisterListener(request.key, sender, request.change_message_id);
  return Status::OK;
}

Status RegistryServer::UnregisterRegistryListener(
    const UnregisterRegistryListenerRequest& request, ProcessId sender) {
  UnregisterListener(sender, request.change_message_id);
  return Status::OK;
}

StatusOr<GetRegistryKeysResponse> RegistryServer::GetRegistryKeys(
    const GetRegistryKeysRequest& request, ProcessId sender) {
  ASSIGN_OR_RETURN(
      auto ns, ResolveAuthorizedNamespace(request.corpus, request.r_namespace,
                                          sender, /*write=*/false));

  GetRegistryKeysResponse response;
  response.keys = ns->GetKeys();
  return response;
}

StatusOr<GetNamespacesResponse> RegistryServer::GetNamespaces(
    ProcessId sender) {
  if (!::perception::DoesProcessHavePermission(
          sender, Permission::CanViewAndModifyEntireRegistry))
    return Status::NOT_ALLOWED;

  GetNamespacesResponse response;
  response.namespaces = ::GetNamespaces();
  return response;
}

Status RegistryServer::FlushRegistry(ProcessId sender) {
  FlushRegistryToDisk();
  return Status::OK;
}
