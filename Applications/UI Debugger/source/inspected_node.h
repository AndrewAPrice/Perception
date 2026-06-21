// Copyright 2026
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

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "include/core/SkBitmap.h"
#include "nlohmann/json.hpp"
#include "perception/ui/components/tree_view.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"

struct InspectedNode {
  std::string id;
  std::string name;
  bool is_hidden;
  ::perception::ui::Point global_position;
  ::perception::ui::Size size;
  ::perception::ui::Point scroll_offset;
  SkBitmap texture;
  std::vector<std::string> handlers;
  std::vector<std::string> components;
  std::vector<std::pair<std::string, std::string>> layout_properties;
  std::map<std::string, std::string> component_properties;
  int z_depth;
  std::string window_title;
  std::vector<std::shared_ptr<InspectedNode>> children;
  std::weak_ptr<InspectedNode> parent;
  std::weak_ptr<::perception::ui::components::TreeViewItem> tree_item;
};

std::shared_ptr<InspectedNode> ParseInspectedNode(
    const ::nlohmann::json& j, int depth, ::perception::ui::Point parent_pos,
    ::perception::ui::Point parent_scroll);

int GetMaxDepth(std::shared_ptr<InspectedNode> node);
