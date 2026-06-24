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

#include "perception/ui/node_serialization.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "nlohmann/json.hpp"
#include "perception/base64.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/text_field.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"

using json = ::nlohmann::json;

namespace perception {
namespace ui {
namespace {

enum class MeasurementType { Point, Percent, Auto };

struct ParsedMeasurement {
  MeasurementType type;
  float value;
};

bool TryParseFloat(std::string_view text, float& out) {
  size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) return false;
  std::string_view trimmed = text.substr(start);
  size_t end = trimmed.find_last_not_of(" \t\r\n");
  trimmed = trimmed.substr(0, end + 1);

  if (trimmed.empty()) return false;
  std::string s(trimmed);
  char* end_ptr = nullptr;
  float val = std::strtof(s.c_str(), &end_ptr);
  if (end_ptr == s.c_str() || std::isnan(val)) return false;
  out = val;
  return true;
}

bool TryParseInt(std::string_view text, int& out) {
  size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) return false;
  std::string_view trimmed = text.substr(start);
  size_t end = trimmed.find_last_not_of(" \t\r\n");
  trimmed = trimmed.substr(0, end + 1);

  if (trimmed.empty()) return false;
  std::string s(trimmed);
  char* end_ptr = nullptr;
  long val = std::strtol(s.c_str(), &end_ptr, 10);
  if (end_ptr == s.c_str()) return false;
  out = static_cast<int>(val);
  return true;
}

bool TryParseUint(std::string_view text, uint32_t& out) {
  size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) return false;
  std::string_view trimmed = text.substr(start);
  size_t end = trimmed.find_last_not_of(" \t\r\n");
  trimmed = trimmed.substr(0, end + 1);

  if (trimmed.empty()) return false;
  std::string s(trimmed);
  char* end_ptr = nullptr;
  unsigned long val = std::strtoul(s.c_str(), &end_ptr, 0);
  if (end_ptr == s.c_str()) return false;
  out = static_cast<uint32_t>(val);
  return true;
}

ParsedMeasurement ParseMeasurement(std::string_view text) {
  size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return {MeasurementType::Auto, 0.0f};
  }
  std::string_view trimmed = text.substr(start);
  size_t end = trimmed.find_last_not_of(" \t\r\n");
  trimmed = trimmed.substr(0, end + 1);

  if (trimmed.empty() || trimmed == "auto" || trimmed == "Auto" ||
      trimmed == "AUTO") {
    return {MeasurementType::Auto, 0.0f};
  }

  bool is_percent = false;
  if (trimmed.back() == '%') {
    is_percent = true;
    trimmed.remove_suffix(1);
    size_t end_pct = trimmed.find_last_not_of(" \t\r\n");
    if (end_pct != std::string_view::npos) {
      trimmed = trimmed.substr(0, end_pct + 1);
    } else {
      return {MeasurementType::Auto, 0.0f};
    }
  }

  float val = 0.0f;
  if (!TryParseFloat(trimmed, val)) {
    return {MeasurementType::Auto, 0.0f};
  }
  return {is_percent ? MeasurementType::Percent : MeasurementType::Point, val};
}

std::string FormatYGValue(YGValue val) {
  if (val.unit == YGUnitPoint) {
    std::string s = std::to_string(val.value);
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
    if (s.back() == '.') s.pop_back();
    return s;
  } else if (val.unit == YGUnitPercent) {
    std::string s = std::to_string(val.value);
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
    if (s.back() == '.') s.pop_back();
    return s + "%";
  } else {
    return "auto";
  }
}

std::string FormatFloat(float val) {
  if (std::isnan(val)) return "0";
  std::string s = std::to_string(val);
  s.erase(s.find_last_not_of('0') + 1, std::string::npos);
  if (s.back() == '.') s.pop_back();
  return s;
}

json NodeToJsonValue(Node& node) {
  json j;
  std::stringstream ss;
  ss << "0x" << std::hex << (uintptr_t)&node;
  j["id"] = ss.str();
  j["hidden"] = node.IsHidden();

  auto layout = node.GetLayout();
  int width = static_cast<int>(layout.GetCalculatedWidth());
  int height = static_cast<int>(layout.GetCalculatedHeight());

  j["calculated_layout"] = {{"left", layout.GetLeft()},
                            {"top", layout.GetTop()},
                            {"width", width},
                            {"height", height}};

  if (node.GetOffset().x != 0.0f || node.GetOffset().y != 0.0f) {
    j["scroll_offset"] = {{"x", node.GetOffset().x},
                          {"y", node.GetOffset().y}};
  }

  if (node.Draws() && width > 0 && height > 0) {
    SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                                         kPremul_SkAlphaType);
    SkBitmap bmp;
    if (bmp.tryAllocPixels(info)) {
      bmp.eraseColor(0x00000000);
      SkCanvas canvas(bmp);
      DrawContext dc;
      dc.skia_canvas = &canvas;
      dc.area =
          Rectangle{{0.0f, 0.0f},
                    {static_cast<float>(width), static_cast<float>(height)}};
      dc.clipping_bounds =
          Rectangle{{0.0f, 0.0f},
                    {static_cast<float>(width), static_cast<float>(height)}};
      node.DrawSelfOnly(dc);

      j["texture"] =
          Base64Encode(reinterpret_cast<const uint8_t*>(bmp.getPixels()),
                       width * height * 4);
    }
  }

  std::vector<std::string> handlers;
  if (node.HasOnDraw()) handlers.push_back("OnDraw");
  if (node.HasOnMouseHover()) handlers.push_back("OnMouseHover");
  if (node.HasOnMouseLeave()) handlers.push_back("OnMouseLeave");
  if (node.HasOnMouseButtonDown()) handlers.push_back("OnMouseButtonDown");
  if (node.HasOnMouseButtonUp()) handlers.push_back("OnMouseButtonUp");
  if (node.HasOnInvalidate()) handlers.push_back("OnInvalidate");
  j["handlers"] = handlers;

  j["components"] = node.GetComponentNames();

  if (auto label = node.Get<components::Label>()) {
    std::stringstream cs;
    cs << "0x" << std::hex << label->GetColor();
    j["component_properties"]["label.text"] = label->GetText();
    j["component_properties"]["label.color"] = cs.str();
  }
  if (auto block = node.Get<components::Block>()) {
    std::stringstream fcs, bcs;
    fcs << "0x" << std::hex << block->GetFillColor();
    bcs << "0x" << std::hex << block->GetBorderColor();
    j["component_properties"]["block.fill_color"] = fcs.str();
    j["component_properties"]["block.border_color"] = bcs.str();
    j["component_properties"]["block.border_width"] =
        std::to_string(block->GetBorderWidth());
    j["component_properties"]["block.border_radius"] =
        std::to_string(block->GetBorderRadius());
  }
  if (auto tf = node.Get<components::TextField>()) {
    j["component_properties"]["text_field.text"] = tf->GetText();
  }
  if (auto cb = node.Get<components::Checkbox>()) {
    j["component_properties"]["checkbox.checked"] =
        cb->IsChecked() ? "true" : "false";
  }

  j["layout"] = {
      {"position_type",
       std::to_string(static_cast<int>(layout.GetPositionType()))},
      {"display", std::to_string(static_cast<int>(layout.GetDisplay()))},
      {"direction", std::to_string(static_cast<int>(layout.GetDirection()))},
      {"overflow", std::to_string(static_cast<int>(layout.GetOverflow()))},
      {"flex_direction",
       std::to_string(static_cast<int>(layout.GetFlexDirection()))},
      {"flex_wrap", std::to_string(static_cast<int>(layout.GetFlexWrap()))},
      {"justify_content",
       std::to_string(static_cast<int>(layout.GetJustifyContent()))},
      {"align_content",
       std::to_string(static_cast<int>(layout.GetAlignContent()))},
      {"align_items", std::to_string(static_cast<int>(layout.GetAlignItems()))},
      {"align_self", std::to_string(static_cast<int>(layout.GetAlignSelf()))},
      {"flex", FormatFloat(layout.GetFlex())},
      {"flex_grow", FormatFloat(layout.GetFlexGrow())},
      {"flex_shrink", FormatFloat(layout.GetFlexShrink())},
      {"aspect_ratio", FormatFloat(layout.GetAspectRatio())},
      {"flex_basis", FormatYGValue(layout.GetFlexBasis())},
      {"width", FormatYGValue(layout.GetWidth())},
      {"height", FormatYGValue(layout.GetHeight())},
      {"min_width", FormatYGValue(layout.GetMinWidth())},
      {"min_height", FormatYGValue(layout.GetMinHeight())},
      {"max_width", FormatYGValue(layout.GetMaxWidth())},
      {"max_height", FormatYGValue(layout.GetMaxHeight())},
      {"left", FormatYGValue(layout.GetPosition(YGEdgeLeft))},
      {"top", FormatYGValue(layout.GetPosition(YGEdgeTop))},
      {"right", FormatYGValue(layout.GetPosition(YGEdgeRight))},
      {"bottom", FormatYGValue(layout.GetPosition(YGEdgeBottom))},
      {"padding_left", FormatYGValue(layout.GetPadding(YGEdgeLeft))},
      {"padding_top", FormatYGValue(layout.GetPadding(YGEdgeTop))},
      {"padding_right", FormatYGValue(layout.GetPadding(YGEdgeRight))},
      {"padding_bottom", FormatYGValue(layout.GetPadding(YGEdgeBottom))},
      {"margin_left", FormatYGValue(layout.GetMargin(YGEdgeLeft))},
      {"margin_top", FormatYGValue(layout.GetMargin(YGEdgeTop))},
      {"margin_right", FormatYGValue(layout.GetMargin(YGEdgeRight))},
      {"margin_bottom", FormatYGValue(layout.GetMargin(YGEdgeBottom))},
      {"border_left", FormatFloat(layout.GetBorder(YGEdgeLeft))},
      {"border_top", FormatFloat(layout.GetBorder(YGEdgeTop))},
      {"border_right", FormatFloat(layout.GetBorder(YGEdgeRight))},
      {"border_bottom", FormatFloat(layout.GetBorder(YGEdgeBottom))},
      {"gap_column", FormatYGValue(layout.GetGap(YGEdgeAll, YGGutterColumn))},
      {"gap_row", FormatYGValue(layout.GetGap(YGEdgeAll, YGGutterRow))}};

  json children_json = json::array();
  for (auto& child : node.GetChildren()) {
    children_json.push_back(NodeToJsonValue(*child));
  }
  j["children"] = children_json;

  return j;
}

std::shared_ptr<Node> NodeFromJsonValue(const json& data) {
  if (!data.is_object()) return Node::Empty();

  std::shared_ptr<Node> node;

  if (data.contains("button")) {
    std::string text = "Button";
    if (data.contains("label") && data["label"].is_object()) {
      if (data["label"].contains("text") && data["label"]["text"].is_string()) {
        text = data["label"]["text"].get<std::string>();
      }
    }
    node = components::Button::TextButton(text, []() {});
  } else if (data.contains("checkbox") && data["checkbox"].is_object()) {
    std::string text = "Checkbox";
    bool checked = false;
    if (data.contains("label") && data["label"].is_object()) {
      if (data["label"].contains("text") && data["label"]["text"].is_string()) {
        text = data["label"]["text"].get<std::string>();
      }
    }
    if (data["checkbox"].contains("checked") &&
        data["checkbox"]["checked"].is_boolean()) {
      checked = data["checkbox"]["checked"].get<bool>();
    }
    node = components::Checkbox::BasicCheckbox(text, checked, [](bool) {});
  } else if (data.contains("text_field") && data["text_field"].is_object()) {
    std::string text = "";
    if (data["text_field"].contains("text") &&
        data["text_field"]["text"].is_string()) {
      text = data["text_field"]["text"].get<std::string>();
    }
    node = components::TextField::BasicTextField(text);
  } else if (data.contains("label") && data["label"].is_object()) {
    std::string text = "";
    uint32 color = 0xFF000000;
    if (data["label"].contains("text") && data["label"]["text"].is_string()) {
      text = data["label"]["text"].get<std::string>();
    }
    if (data["label"].contains("color") &&
        data["label"]["color"].is_number_integer()) {
      color = data["label"]["color"].get<uint32>();
    }
    node = components::Label::BasicLabel(
        text, [color](components::Label& l) { l.SetColor(color); });
  } else {
    node = Node::Empty();
  }

  if (data.contains("block") && data["block"].is_object()) {
    auto block_data = data["block"];
    auto block = node->GetOrAdd<components::Block>();
    if (block_data.contains("fill_color") &&
        block_data["fill_color"].is_number_integer()) {
      block->SetFillColor(block_data["fill_color"].get<uint32>());
    }
    if (block_data.contains("border_color") &&
        block_data["border_color"].is_number_integer()) {
      block->SetBorderColor(block_data["border_color"].get<uint32>());
    }
    if (block_data.contains("border_width") &&
        block_data["border_width"].is_number()) {
      block->SetBorderWidth(block_data["border_width"].get<float>());
    }
    if (block_data.contains("border_radius") &&
        block_data["border_radius"].is_number()) {
      block->SetBorderRadius(block_data["border_radius"].get<float>());
    }
  }

  if (data.contains("layout") && data["layout"].is_object()) {
    for (auto& [k, v] : data["layout"].items()) {
      std::string val_str;
      if (v.is_string())
        val_str = v.get<std::string>();
      else if (v.is_number_integer())
        val_str = std::to_string(v.get<int>());
      else if (v.is_number_float())
        val_str = std::to_string(v.get<float>());
      else if (v.is_boolean())
        val_str = v.get<bool>() ? "true" : "false";
      if (!val_str.empty()) {
        node->TweakProperty("layout." + k, val_str);
      }
    }
  }

  if (data.contains("children") && data["children"].is_array() &&
      !data.contains("button") && !data.contains("text_field") &&
      !data.contains("checkbox")) {
    for (const auto& child_data : data["children"]) {
      auto child = NodeFromJsonValue(child_data);
      if (child) node->AddChild(child);
    }
  }

  return node;
}
}  // namespace

std::string SerializeNode(Node& node, std::string_view window_title) {
  return node.ToJson(window_title);
}

std::shared_ptr<Node> DeserializeNode(std::string_view json_str) {
  return Node::FromJson(json_str);
}

void TweakNodeProperty(Node& node, std::string_view property_name,
                       std::string_view property_value) {
  node.TweakProperty(property_name, property_value);
}

std::string Node::ToJson(std::string_view window_title) {
  json j = NodeToJsonValue(*this);
  if (!window_title.empty()) {
    j["window_title"] = window_title;
  }
  return j.dump();
}

std::shared_ptr<Node> Node::FromJson(std::string_view json_str) {
  if (json_str.empty()) return Node::Empty();
  try {
    json data = json::parse(json_str);
    return NodeFromJsonValue(data);
  } catch (...) {
    return Node::Empty();
  }
}

void Node::TweakProperty(std::string_view property_name,
                         std::string_view property_value) {
  if (property_name == "hidden") {
    bool hide = (property_value == "true" || property_value == "True" ||
                 property_value == "1");
    YGNodeStyleSetDisplay(yoga_node_, hide ? YGDisplayNone : YGDisplayFlex);
    Invalidate();
    return;
  }

  if (property_name.size() > 7 && property_name.substr(0, 7) == "layout.") {
    std::string_view prop = property_name.substr(7);
    Layout layout = GetLayout();
    int i_val = 0;
    float f_val = 0.0f;
    if (prop == "position_type") {
      if (TryParseInt(property_value, i_val))
        layout.SetPositionType(static_cast<YGPositionType>(i_val));
    } else if (prop == "display") {
      if (TryParseInt(property_value, i_val))
        layout.SetDisplay(static_cast<YGDisplay>(i_val));
    } else if (prop == "direction") {
      if (TryParseInt(property_value, i_val))
        layout.SetDirection(static_cast<YGDirection>(i_val));
    } else if (prop == "overflow") {
      if (TryParseInt(property_value, i_val))
        layout.SetOverflow(static_cast<YGOverflow>(i_val));
    } else if (prop == "flex_direction") {
      if (TryParseInt(property_value, i_val))
        layout.SetFlexDirection(static_cast<YGFlexDirection>(i_val));
    } else if (prop == "flex_wrap") {
      if (TryParseInt(property_value, i_val))
        layout.SetFlexWrap(static_cast<YGWrap>(i_val));
    } else if (prop == "justify_content") {
      if (TryParseInt(property_value, i_val))
        layout.SetJustifyContent(static_cast<YGJustify>(i_val));
    } else if (prop == "align_content") {
      if (TryParseInt(property_value, i_val))
        layout.SetAlignContent(static_cast<YGAlign>(i_val));
    } else if (prop == "align_items") {
      if (TryParseInt(property_value, i_val))
        layout.SetAlignItems(static_cast<YGAlign>(i_val));
    } else if (prop == "align_self") {
      if (TryParseInt(property_value, i_val))
        layout.SetAlignSelf(static_cast<YGAlign>(i_val));
    } else if (prop == "flex") {
      if (TryParseFloat(property_value, f_val)) layout.SetFlex(f_val);
    } else if (prop == "flex_grow") {
      if (TryParseFloat(property_value, f_val)) layout.SetFlexGrow(f_val);
    } else if (prop == "flex_shrink") {
      if (TryParseFloat(property_value, f_val)) layout.SetFlexShrink(f_val);
    } else if (prop == "aspect_ratio") {
      if (TryParseFloat(property_value, f_val)) layout.SetAspectRatio(f_val);
    } else if (prop == "border_left") {
      if (TryParseFloat(property_value, f_val))
        layout.SetBorder(YGEdgeLeft, f_val);
    } else if (prop == "border_top") {
      if (TryParseFloat(property_value, f_val))
        layout.SetBorder(YGEdgeTop, f_val);
    } else if (prop == "border_right") {
      if (TryParseFloat(property_value, f_val))
        layout.SetBorder(YGEdgeRight, f_val);
    } else if (prop == "border_bottom") {
      if (TryParseFloat(property_value, f_val))
        layout.SetBorder(YGEdgeBottom, f_val);
    } else if (prop == "gap_column") {
      if (TryParseFloat(property_value, f_val))
        layout.SetGap(f_val, YGGutterColumn);
    } else if (prop == "gap_row") {
      if (TryParseFloat(property_value, f_val))
        layout.SetGap(f_val, YGGutterRow);
    } else {
      auto m = ParseMeasurement(property_value);
      if (prop == "width") {
        if (m.type == MeasurementType::Point)
          layout.SetWidth(m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetWidthPercent(m.value);
        else
          layout.SetWidthAuto();
      } else if (prop == "height") {
        if (m.type == MeasurementType::Point)
          layout.SetHeight(m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetHeightPercent(m.value);
        else
          layout.SetHeightAuto();
      } else if (prop == "min_width") {
        if (m.type == MeasurementType::Point)
          layout.SetMinWidth(m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetMinWidthPercent(m.value);
        else
          layout.SetMinWidthAuto();
      } else if (prop == "min_height") {
        if (m.type == MeasurementType::Point)
          layout.SetMinHeight(m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetMinHeightPercent(m.value);
        else
          layout.SetMinHeightAuto();
      } else if (prop == "max_width") {
        if (m.type == MeasurementType::Point)
          layout.SetMaxWidth(m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetMaxWidthPercent(m.value);
        else
          layout.SetMaxWidthAuto();
      } else if (prop == "max_height") {
        if (m.type == MeasurementType::Point)
          layout.SetMaxHeight(m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetMaxHeightPercent(m.value);
        else
          layout.SetMaxHeightAuto();
      } else if (prop == "flex_basis") {
        if (m.type == MeasurementType::Point)
          layout.SetFlexBasis(m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetFlexBasisPercent(m.value);
        else
          layout.SetFlexBasisAuto();
      } else if (prop == "left") {
        if (m.type == MeasurementType::Point)
          layout.SetPosition(YGEdgeLeft, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetPositionPercent(YGEdgeLeft, m.value);
        else
          layout.SetPositionAuto(YGEdgeLeft);
      } else if (prop == "top") {
        if (m.type == MeasurementType::Point)
          layout.SetPosition(YGEdgeTop, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetPositionPercent(YGEdgeTop, m.value);
        else
          layout.SetPositionAuto(YGEdgeTop);
      } else if (prop == "right") {
        if (m.type == MeasurementType::Point)
          layout.SetPosition(YGEdgeRight, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetPositionPercent(YGEdgeRight, m.value);
        else
          layout.SetPositionAuto(YGEdgeRight);
      } else if (prop == "bottom") {
        if (m.type == MeasurementType::Point)
          layout.SetPosition(YGEdgeBottom, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetPositionPercent(YGEdgeBottom, m.value);
        else
          layout.SetPositionAuto(YGEdgeBottom);
      } else if (prop == "margin_left") {
        if (m.type == MeasurementType::Point)
          layout.SetMargin(YGEdgeLeft, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetMarginPercent(YGEdgeLeft, m.value);
        else
          layout.SetMarginAuto(YGEdgeLeft);
      } else if (prop == "margin_top") {
        if (m.type == MeasurementType::Point)
          layout.SetMargin(YGEdgeTop, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetMarginPercent(YGEdgeTop, m.value);
        else
          layout.SetMarginAuto(YGEdgeTop);
      } else if (prop == "margin_right") {
        if (m.type == MeasurementType::Point)
          layout.SetMargin(YGEdgeRight, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetMarginPercent(YGEdgeRight, m.value);
        else
          layout.SetMarginAuto(YGEdgeRight);
      } else if (prop == "margin_bottom") {
        if (m.type == MeasurementType::Point)
          layout.SetMargin(YGEdgeBottom, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetMarginPercent(YGEdgeBottom, m.value);
        else
          layout.SetMarginAuto(YGEdgeBottom);
      } else if (prop == "padding_left") {
        if (m.type == MeasurementType::Point)
          layout.SetPadding(YGEdgeLeft, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetPaddingPercent(YGEdgeLeft, m.value);
        else
          layout.SetPaddingAuto(YGEdgeLeft);
      } else if (prop == "padding_top") {
        if (m.type == MeasurementType::Point)
          layout.SetPadding(YGEdgeTop, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetPaddingPercent(YGEdgeTop, m.value);
        else
          layout.SetPaddingAuto(YGEdgeTop);
      } else if (prop == "padding_right") {
        if (m.type == MeasurementType::Point)
          layout.SetPadding(YGEdgeRight, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetPaddingPercent(YGEdgeRight, m.value);
        else
          layout.SetPaddingAuto(YGEdgeRight);
      } else if (prop == "padding_bottom") {
        if (m.type == MeasurementType::Point)
          layout.SetPadding(YGEdgeBottom, m.value);
        else if (m.type == MeasurementType::Percent)
          layout.SetPaddingPercent(YGEdgeBottom, m.value);
        else
          layout.SetPaddingAuto(YGEdgeBottom);
      }
    }
    Invalidate();
    return;
  }

  if (property_name.size() > 6 && property_name.substr(0, 6) == "label.") {
    std::string_view prop = property_name.substr(6);
    if (auto label = Get<components::Label>()) {
      if (prop == "text") {
        label->SetText(std::string(property_value));
      } else if (prop == "color") {
        uint32_t c_val = 0;
        if (TryParseUint(property_value, c_val)) label->SetColor(c_val);
      }
    }
    return;
  }

  if (property_name.size() > 6 && property_name.substr(0, 6) == "block.") {
    std::string_view prop = property_name.substr(6);
    if (auto block = Get<components::Block>()) {
      uint32_t c_val = 0;
      float f_val = 0.0f;
      if (prop == "fill_color") {
        if (TryParseUint(property_value, c_val)) block->SetFillColor(c_val);
      } else if (prop == "border_color") {
        if (TryParseUint(property_value, c_val)) block->SetBorderColor(c_val);
      } else if (prop == "border_width") {
        if (TryParseFloat(property_value, f_val)) block->SetBorderWidth(f_val);
      } else if (prop == "border_radius") {
        if (TryParseFloat(property_value, f_val)) block->SetBorderRadius(f_val);
      }
    }
    return;
  }

  if (property_name.size() > 11 &&
      property_name.substr(0, 11) == "text_field.") {
    std::string_view prop = property_name.substr(11);
    if (auto tf = Get<components::TextField>()) {
      if (prop == "text") {
        tf->SetText(std::string(property_value));
      }
    }
    return;
  }

  if (property_name.size() > 9 && property_name.substr(0, 9) == "checkbox.") {
    std::string_view prop = property_name.substr(9);
    if (auto cb = Get<components::Checkbox>()) {
      if (prop == "checked") {
        bool checked = (property_value == "true" || property_value == "True" ||
                        property_value == "1");
        cb->SetChecked(checked);
      }
    }
    return;
  }
}

}  // namespace ui
}  // namespace perception
