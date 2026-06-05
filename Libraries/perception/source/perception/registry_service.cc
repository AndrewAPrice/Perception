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

#include "perception/registry_service.h"

#include "perception/serialization/serializer.h"

namespace perception {

void GetRegistryValueRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Enum("corpus", corpus);
  serializer.String("namespace", r_namespace);
  serializer.String("key", key);
}

void GetRegistryValueResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.Serializable("value", value);
}

void SetRegistryValueRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Enum("corpus", corpus);
  serializer.String("namespace", r_namespace);
  serializer.String("key", key);
  serializer.Serializable("value", value);
}

void DeleteRegistryValueRequest::Serialize(
    serialization::Serializer& serializer) {
  serializer.Enum("corpus", corpus);
  serializer.String("namespace", r_namespace);
  serializer.String("key", key);
}

void RegisterRegistryListenerRequest::Serialize(
    serialization::Serializer& serializer) {
  serializer.Enum("corpus", corpus);
  serializer.String("namespace", r_namespace);
  serializer.String("key", key);
  serializer.Integer("change_message_id", change_message_id);
}

void UnregisterRegistryListenerRequest::Serialize(
    serialization::Serializer& serializer) {
  serializer.Integer("change_message_id", change_message_id);
}

void GetRegistryKeysRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Enum("corpus", corpus);
  serializer.String("namespace", r_namespace);
}

void GetRegistryKeysResponse::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfStrings("keys", keys);
}

void NamespaceInfo::Serialize(serialization::Serializer& serializer) {
  serializer.Enum("corpus", corpus);
  serializer.String("name", name);
}

void GetNamespacesResponse::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("namespaces", namespaces);
}

}  // namespace perception
