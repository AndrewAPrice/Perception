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
#include <string>
#include <vector>

#include "perception/serialization/value.h"
#include "perception/service_macros.h"

namespace perception {

enum class RegistryCorpus : uint8 { APPLICATIONS = 0, LIBRARIES = 1 };

class GetRegistryValueRequest : public serialization::Serializable {
 public:
  uint8 corpus;
  std::string r_namespace;
  std::string key;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class GetRegistryValueResponse : public serialization::Serializable {
 public:
  serialization::Value value;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class SetRegistryValueRequest : public serialization::Serializable {
 public:
  uint8 corpus;
  std::string r_namespace;
  std::string key;
  serialization::Value value;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class DeleteRegistryValueRequest : public serialization::Serializable {
 public:
  uint8 corpus;
  std::string r_namespace;
  std::string key;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class RegisterRegistryListenerRequest : public serialization::Serializable {
 public:
  uint8 corpus;
  std::string r_namespace;
  std::string key;
  MessageId change_message_id;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class UnregisterRegistryListenerRequest : public serialization::Serializable {
 public:
  MessageId change_message_id;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class GetRegistryKeysRequest : public serialization::Serializable {
 public:
  uint8 corpus;
  std::string r_namespace;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class GetRegistryKeysResponse : public serialization::Serializable {
 public:
  std::vector<std::string> keys;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class NamespaceInfo : public serialization::Serializable {
 public:
  uint8 corpus;
  std::string name;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class GetNamespacesResponse : public serialization::Serializable {
 public:
  std::vector<NamespaceInfo> namespaces;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                                                      \
  X(1, GetRegistryValue, GetRegistryValueResponse, GetRegistryValueRequest) \
  X(2, SetRegistryValue, void, SetRegistryValueRequest)                     \
  X(3, DeleteRegistryValue, void, DeleteRegistryValueRequest)               \
  X(4, RegisterRegistryListener, void, RegisterRegistryListenerRequest)     \
  X(5, UnregisterRegistryListener, void, UnregisterRegistryListenerRequest) \
  X(6, GetRegistryKeys, GetRegistryKeysResponse, GetRegistryKeysRequest)    \
  X(7, GetNamespaces, GetNamespacesResponse, void)
DEFINE_PERCEPTION_SERVICE(Registry, "perception.core.Registry", METHOD_LIST)
#undef METHOD_LIST

}  // namespace perception
