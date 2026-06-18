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

#include "perception/window/base_window.h"

#include "perception/serialization/serializer.h"

namespace perception {
namespace window {

void DebugUiHierarchy::Serialize(serialization::Serializer& serializer) {
  serializer.String("JSON", json);
}

void CreateNodeResponse::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Temp ID", temp_id);
  serializer.Integer("Real ID", real_id);
}

void TweakUiResponse::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Create Node Responses",
                                  create_node_responses);
}

void NodePropertyTweak::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Node ID", node_id);
  serializer.String("Property Name", property_name);
  serializer.String("Property Value", property_value);
}

void CreateNode::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Temp ID", temp_id);
  serializer.Integer("Parent Node ID", parent_node_id);
}

void DeleteNode::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Node ID", node_id);
}

void ReparentNode::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Node ID", node_id);
  serializer.Integer("New Parent Node ID", new_parent_node_id);
}

void TweakUiRequest::Serialize(serialization::Serializer& serializer) {
  serializer.ArrayOfSerializables("Property Tweaks", property_tweaks);
  serializer.ArrayOfSerializables("Create Nodes", create_nodes);
  serializer.ArrayOfSerializables("Delete Nodes", delete_nodes);
  serializer.ArrayOfSerializables("Reparent Nodes", reparent_nodes);
}

}  // namespace window
}  // namespace perception
