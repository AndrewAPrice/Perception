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

using StatusOr;
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
using ::perception::SetRegistryValueRequest;
using ::perception::Status;
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
  ASSIGN_OR_RETURN(
      auto ns, ResolveAuthorizedNamespace(request.corpus, request.r_namespace,
                                          sender, /*write=*/true));

  ns->SetValue(request.key, request.value);
  ns->NotifyListeners(request.key);
  return Status::OK;
}

Status RegistryServer::DeleteRegistryValue(
    const DeleteRegistryValueRequest& request, ProcessId sender) {
  ASSIGN_OR_RETURN(
      auto ns, ResolveAuthorizedNamespace(request.corpus, request.r_namespace,
                                          sender, /*write=*/true));

  if (ns->DeleteValue(request.key)) ns->NotifyListeners(request.key);
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
