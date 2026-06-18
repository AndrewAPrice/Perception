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
#include <string_view>

namespace perception {
namespace ui {

class Node;

// Serializes the node hierarchy and its components to a JSON string.
std::string SerializeNode(Node& node, std::string_view window_title = "");

// Constructs a node hierarchy from a JSON string.
std::shared_ptr<Node> DeserializeNode(std::string_view json_str);

// Tweaks a property on a node or its components from string values.
void TweakNodeProperty(Node& node, std::string_view property_name,
                       std::string_view property_value);

}  // namespace ui
}  // namespace perception
