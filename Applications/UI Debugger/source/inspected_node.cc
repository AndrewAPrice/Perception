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

#include "inspected_node.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "nlohmann/json.hpp"

using json = ::nlohmann::json;

namespace {

std::vector<uint8_t> Base64Decode(std::string_view encoded) {
  std::vector<uint8_t> ret;
  ret.reserve((encoded.size() / 4) * 3);

  auto get_val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };

  uint32_t accum = 0;
  int bits = 0;
  for (char c : encoded) {
    if (c == '=') break;
    int val = get_val(c);
    if (val < 0) continue;
    accum = (accum << 6) | val;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      ret.push_back(static_cast<uint8_t>((accum >> bits) & 0xFF));
    }
  }
  return ret;
}

}  // namespace

std::shared_ptr<InspectedNode> ParseInspectedNode(
    const json& j, int depth, ::perception::ui::Point parent_pos,
    ::perception::ui::Point parent_scroll) {
  auto node = std::make_shared<InspectedNode>();
  node->z_depth = depth;
  if (depth == 0 && j.contains("window_title") && j["window_title"].is_string())
    node->window_title = j["window_title"].get<std::string>();
  if (j.contains("id") && j["id"].is_string())
    node->id = j["id"].get<std::string>();
  if (j.contains("hidden") && j["hidden"].is_boolean())
    node->is_hidden = j["hidden"].get<bool>();

  float left = 0, top = 0, width = 0, height = 0;
  if (j.contains("calculated_layout") && j["calculated_layout"].is_object()) {
    auto cl = j["calculated_layout"];
    if (cl.contains("left") && cl["left"].is_number())
      left = cl["left"].get<float>();
    if (cl.contains("top") && cl["top"].is_number())
      top = cl["top"].get<float>();
    if (cl.contains("width") && cl["width"].is_number())
      width = cl["width"].get<float>();
    if (cl.contains("height") && cl["height"].is_number())
      height = cl["height"].get<float>();
  }

  node->global_position =
      parent_pos + ::perception::ui::Point{left, top} - parent_scroll;
  node->size = {width, height};
  node->scroll_offset = {0, 0};

  if (j.contains("scroll_offset") && j["scroll_offset"].is_object()) {
    auto so = j["scroll_offset"];
    if (so.contains("x") && so["x"].is_number())
      node->scroll_offset.x = so["x"].get<float>();
    if (so.contains("y") && so["y"].is_number())
      node->scroll_offset.y = so["y"].get<float>();
  }

  if (j.contains("texture") && j["texture"].is_string()) {
    std::string b64 = j["texture"].get<std::string>();
    if (!b64.empty() && width > 0 && height > 0) {
      auto bytes = Base64Decode(b64);
      int w = static_cast<int>(width);
      int h = static_cast<int>(height);
      if (bytes.size() >= static_cast<size_t>(w * h * 4)) {
        SkImageInfo info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType,
                                             kPremul_SkAlphaType);
        SkBitmap bmp;
        if (bmp.tryAllocPixels(info)) {
          memcpy(bmp.getPixels(), bytes.data(), w * h * 4);
          node->texture = bmp;
        }
      }
    }
  }

  if (j.contains("handlers") && j["handlers"].is_array()) {
    for (auto& h : j["handlers"]) {
      if (h.is_string()) node->handlers.push_back(h.get<std::string>());
    }
  }

  if (j.contains("components") && j["components"].is_array()) {
    for (auto& c : j["components"]) {
      if (c.is_string()) node->components.push_back(c.get<std::string>());
    }
  }

  if (!node->components.empty()) {
    std::string comp = node->components.front();
    if (node->components.size() > 1) {
      for (const auto& c : node->components) {
        if (c != "Block" &&
            (c.size() < 7 || c.substr(c.size() - 7) != "::Block")) {
          comp = c;
          break;
        }
      }
    }
    size_t last_colon = comp.rfind("::");
    if (last_colon != std::string::npos) {
      node->name = comp.substr(last_colon + 2);
    } else {
      node->name = comp;
    }
  } else {
    node->name = "Node";
  }

  if (j.contains("layout") && j["layout"].is_object()) {
    for (auto& [k, v] : j["layout"].items()) {
      if (v.is_string()) {
        node->layout_properties.push_back({k, v.get<std::string>()});
      } else if (v.is_number()) {
        node->layout_properties.push_back({k, std::to_string(v.get<float>())});
      }
    }
  }

  if (j.contains("component_properties") &&
      j["component_properties"].is_object()) {
    for (auto& [k, v] : j["component_properties"].items()) {
      if (v.is_string()) {
        node->component_properties[k] = v.get<std::string>();
      }
    }
  }

  if (j.contains("children") && j["children"].is_array()) {
    for (auto& c : j["children"]) {
      auto child = ParseInspectedNode(c, depth + 1, node->global_position,
                                      node->scroll_offset);
      if (child) {
        child->parent = node;
        node->children.push_back(child);
      }
    }
  }

  return node;
}

int GetMaxDepth(std::shared_ptr<InspectedNode> node) {
  if (!node) return 0;
  int d = node->z_depth;
  for (auto& child : node->children) d = std::max(d, GetMaxDepth(child));
  return d;
}
