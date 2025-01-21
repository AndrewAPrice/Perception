// Copyright 2024 Google LLC
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

#include "include/core/SkFont.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "perception/ui/text_alignment.h"
#include "yoga/Yoga.h"

namespace perception {
namespace ui {

struct DrawContext;

namespace components {

// A label draws a piece of text.
class Label {
 public:
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicLabel(std::string_view text,
                                          Modifiers... modifiers) {
    return Node::Empty([text](Label& label) { label.SetText(text); },
                       modifiers...);
  }

  Label();

  void SetNode(std::weak_ptr<Node> node);

  void SetFont(SkFont* font);
  SkFont* GetFont() const;

  void SetColor(uint32_t color);
  uint32_t GetColor() const;

  void SetText(std::string_view text);
  std::string_view GetText() const;

  void SetTextAlignment(TextAlignment text_alignment);
  TextAlignment GetTextAlignment();

 private:
  SkFont* font_;
  uint32_t color_;
  std::string text_;
  TextAlignment text_alignment_;
  bool text_needs_realignment_;
  Size size_;
  Point offset_;
  std::weak_ptr<Node> node_;

  void Draw(const DrawContext& draw_context);
  Size Measure(float width, YGMeasureMode width_mode, float height,
               YGMeasureMode height_mode);
  void CalculateTextAlignmentOffsetsIfNeeded();
  void AssignDefaultFontIfUnassigned();
};

}  // namespace components
}  // namespace ui
}  // namespace perception