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
#include <vector>

#include "include/core/SkFont.h"
#include "perception/type_id.h"
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
class Label : public UniqueIdentifiableType<Label> {
 public:
  enum class TruncationMode {
    // Keep wrapping text normally, clipping/overflowing if it exceeds bounds.
    None = 0,

    // Truncate at the last whole character that fits the layout width.
    LastWholeCharacter = 1,

    // Truncate at the last character/word that fits and append a unicode
    // ellipsis "…".
    Ellipsis = 2
  };

  // A basic label.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicLabel(std::string_view text,
                                          Modifiers... modifiers) {
    return Node::Empty([text](Label& label) { label.SetText(text); },
                       modifiers...);
  }

  // A single line label that will truncate the text with an ellipsis if it is
  // too long.
  template <typename... Modifiers>
  static std::shared_ptr<Node> SingleLineTruncated(std::string_view text,
                                                   Modifiers... modifiers) {
    return Node::Empty(
        [text](Label& label) {
          label.SetText(text);
          label.SetMaxLines(1);
          label.SetTruncationMode(TruncationMode::Ellipsis);
        },
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

  // Sets the maximum number of lines to render.
  // If max_lines is 0, there is no limit. Defaults to 0.
  void SetMaxLines(int max_lines);
  int GetMaxLines() const;

  void SetTruncationMode(TruncationMode truncation_mode);
  TruncationMode GetTruncationMode() const;

 private:
  struct LaidOutLine {
    std::string_view text_view;
    std::string mutated_text;
    Size size;

    std::string_view GetText() const {
      if (!mutated_text.empty()) {
        return mutated_text;
      }
      return text_view;
    }
  };

  SkFont* font_;
  uint32_t color_;
  std::string text_;
  TextAlignment text_alignment_;
  bool text_needs_realignment_;
  Size size_;
  Point offset_;
  std::weak_ptr<Node> node_;

  int max_lines_;
  TruncationMode truncation_mode_;
  std::vector<LaidOutLine> laid_out_lines_;
  float last_layout_width_;
  bool layout_is_dirty_;

  void Draw(const DrawContext& draw_context);
  Size Measure(float width, YGMeasureMode width_mode, float height,
               YGMeasureMode height_mode);
  void CalculateTextAlignmentOffsetsIfNeeded();
  void AssignDefaultFontIfUnassigned();
  void LayoutText(float max_width);
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::Label>;

}  // namespace perception