// Copyright 2025 Google LLC
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

#include <string>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/window/size.h"

namespace perception {
namespace window {

class DebugUiHierarchy : public serialization::Serializable {
 public:
  std::string json;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class CreateNodeResponse : public serialization::Serializable {
 public:
  uint64 temp_id;
  uint64 real_id;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class TweakUiResponse : public serialization::Serializable {
 public:
  std::vector<CreateNodeResponse> create_node_responses;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class NodePropertyTweak : public serialization::Serializable {
 public:
  uint64 node_id;
  std::string property_name;
  std::string property_value;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class CreateNode : public serialization::Serializable {
 public:
  uint64 temp_id;
  uint64 parent_node_id;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class DeleteNode : public serialization::Serializable {
 public:
  uint64 node_id;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class ReparentNode : public serialization::Serializable {
 public:
  uint64 node_id;
  uint64 new_parent_node_id;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class TweakUiRequest : public serialization::Serializable {
 public:
  std::vector<NodePropertyTweak> property_tweaks;
  std::vector<CreateNode> create_nodes;
  std::vector<DeleteNode> delete_nodes;
  std::vector<ReparentNode> reparent_nodes;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X) \
  X(1, SetSize, void, Size) \
  X(2, Closed, void, void) \
  X(3, GainedFocus, void, void) \
  X(4, LostFocus, void, void) \
  X(5, DisplayEnvironmentChanged, void, void) \
  X(6, GetUiHierarchy, DebugUiHierarchy, void) \
  X(7, TweakUi, TweakUiResponse, TweakUiRequest)

DEFINE_PERCEPTION_SERVICE(BaseWindow, "perception.window.BaseWindow",
                          METHOD_LIST)

#undef METHOD_LIST

}  // namespace window
}  // namespace perception