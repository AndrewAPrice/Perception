// Copyright 2021 Google LLC
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

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "perception/ui/text_alignment.h"
#include "perception/ui/widget.h"

class SkFont;

namespace perception {
namespace ui {

struct DrawContext;

class Label : public Widget {
 public:
  Label();
  virtual ~Label();

  Label *SetFont(SkFont *font);
  SkFont *GetFont();
  Label* SetLabel(std::string_view label);
  std::string_view GetLabel();
  Label* SetTextAlignment(TextAlignment alignment);
  Label* SetColor(uint32_t color);
  uint32 GetColor();

 private:
  virtual void Draw(DrawContext& draw_context) override;

  static YGSize Measure(const YGNode* node, float width,
                        YGMeasureMode width_mode, float height,
                        YGMeasureMode height_mode);

  static void LayoutDirtied(const YGNode* node);
  void AssignDefaultFontIfUnassigned();

  uint32_t color_;
  std::string label_;
  TextAlignment text_alignment_;
  bool realign_text_;
  float text_x_, text_y_;
  SkFont *font_;
};

}  // namespace ui
}  // namespace perception
