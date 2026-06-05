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

#include "perception/registry.h"

#include "perception/messages.h"
#include "perception/services.h"

namespace perception {

StatusOr<serialization::Value> GetRegistryValue(std::string_view key) {
  return GetRegistryValue(RegistryCorpus::APPLICATIONS, "", key);
}

StatusOr<serialization::Value> GetRegistryValue(RegistryCorpus corpus,
                                                std::string_view r_namespace,
                                                std::string_view key) {
  GetRegistryValueRequest request;
  request.corpus = corpus;
  request.r_namespace = std::string(r_namespace);
  request.key = std::string(key);

  ASSIGN_OR_RETURN(auto response,
                   GetService<Registry>().GetRegistryValue(request));
  return response.value;
}

void SetRegistryValue(std::string_view key, const serialization::Value& value) {
  return SetRegistryValue(RegistryCorpus::APPLICATIONS, "", key, value);
}

void SetRegistryValue(RegistryCorpus corpus, std::string_view r_namespace,
                      std::string_view key, const serialization::Value& value) {
  SetRegistryValueRequest request;
  request.corpus = corpus;
  request.r_namespace = std::string(r_namespace);
  request.key = std::string(key);
  request.value = value;

  GetService<Registry>().SetRegistryValue(request);
}

void DeleteRegistryValue(std::string_view key) {
  return DeleteRegistryValue(RegistryCorpus::APPLICATIONS, "", key);
}

void DeleteRegistryValue(RegistryCorpus corpus, std::string_view r_namespace,
                         std::string_view key) {
  DeleteRegistryValueRequest request;
  request.corpus = corpus;
  request.r_namespace = std::string(r_namespace);
  request.key = std::string(key);

  GetService<Registry>().DeleteRegistryValue(request);
}

StatusOr<RegistryListenerToken> RegisterRegistryListener(
    std::string_view key, std::function<void()> callback) {
  return RegisterRegistryListener(RegistryCorpus::APPLICATIONS, "", key,
                                  callback);
}

StatusOr<RegistryListenerToken> RegisterRegistryListener(
    RegistryCorpus corpus, std::string_view r_namespace, std::string_view key,
    std::function<void()> callback) {
  MessageId change_message_id = GenerateUniqueMessageId();
  RegisterMessageHandler(
      change_message_id,
      [callback](ProcessId, const MessageData&) { callback(); });

  RegisterRegistryListenerRequest request;
  request.corpus = corpus;
  request.r_namespace = std::string(r_namespace);
  request.key = std::string(key);
  request.change_message_id = change_message_id;

  auto status = GetService<Registry>().RegisterRegistryListener(request);
  if (status != Status::OK) {
    UnregisterMessageHandler(change_message_id);
    return status;
  }

  return change_message_id;
}

Status UnregisterRegistryListener(RegistryListenerToken token) {
  UnregisterMessageHandler(token);

  UnregisterRegistryListenerRequest request;
  request.change_message_id = token;

  return GetService<Registry>().UnregisterRegistryListener(request);
}

StatusOr<std::vector<std::string>> GetRegistryKeys() {
  return GetRegistryKeys(RegistryCorpus::APPLICATIONS, "");
}

StatusOr<std::vector<std::string>> GetRegistryKeys(
    RegistryCorpus corpus, std::string_view r_namespace) {
  GetRegistryKeysRequest request;
  request.corpus = corpus;
  request.r_namespace = std::string(r_namespace);

  ASSIGN_OR_RETURN(auto response,
                   GetService<Registry>().GetRegistryKeys(request));
  return std::move(response.keys);
}

StatusOr<std::vector<NamespaceInfo>> GetNamespacesInRegistry() {
  ASSIGN_OR_RETURN(auto response, GetService<Registry>().GetNamespaces());
  return response.namespaces;
}

}  // namespace perception
