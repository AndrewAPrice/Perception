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

#include "perception/ui/components/label.h"

#include <memory>

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/theme.h"
#include "perception/ui/measurements.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::Label>;

namespace ui {
namespace components {

Label::Label()
    : color_(kLabelTextColor),
      text_alignment_(TextAlignment::TopLeft),
      text_needs_realignment_(true) {}

void Label::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node.expired()) return;
  auto strong_node = node.lock();
  strong_node->OnDraw(std::bind_front(&Label::Draw, this));
  strong_node->SetMeasureFunction(std::bind_front(&Label::Measure, this));
}

void Label::SetTextAlignment(TextAlignment text_alignment) {
  if (text_alignment == text_alignment_) return;
  text_alignment_ = text_alignment;
  if (!node_.expired()) node_.lock()->Invalidate();
}

void Label::SetFont(SkFont* font) {
  if (font_ == font) return;
  font_ = font;
  text_needs_realignment_ = true;
  if (!node_.expired()) {
    auto node = node_.lock();
    node->Remeasure();
    node->Invalidate();
  }
}

SkFont* Label::GetFont() const { return font_; }

void Label::SetColor(uint32_t color) {
  if (color_ == color) return;
  color_ = color;
  if (!node_.expired()) node_.lock()->Invalidate();
}

uint32_t Label::GetColor() const { return color_; }

void Label::SetText(std::string_view text) {
  if (text_ == text) return;
  text_ = text;
  text_needs_realignment_ = true;
  if (!node_.expired()) {
    auto node = node_.lock();
    node->Remeasure();
    node->Invalidate();
  }
}

std::string_view Label::GetText() const { return text_; }

void Label::Draw(const DrawContext& draw_context) {
  AssignDefaultFontIfUnassigned();
  if (draw_context.area.size != size_) {
    size_ = draw_context.area.size;
    text_needs_realignment_ = true;
  }
  CalculateTextAlignmentOffsetsIfNeeded();

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(color_);

  Point position = draw_context.area.origin + offset_;
  draw_context.skia_canvas->drawString(SkString(text_), position.x, position.y,
                                       *font_, paint);
}

Size Label::Measure(float width, YGMeasureMode width_mode, float height,
                    YGMeasureMode height_mode) {
  if (text_.empty()) {
    return {.width = CalculateMeasuredLength(width_mode, width, 0.0f),
            .height = CalculateMeasuredLength(height_mode, height, 0.0f)};
  }

  AssignDefaultFontIfUnassigned();

  SkRect string_bounds;
  (void)font_->measureText(&text_[0], text_.length(), SkTextEncoding::kUTF8,
                           &string_bounds);
  return {.width =
              CalculateMeasuredLength(width_mode, width, string_bounds.width()),
          .height = CalculateMeasuredLength(height_mode, height,
                                            string_bounds.height())};
}

void Label::CalculateTextAlignmentOffsetsIfNeeded() {
  if (!text_needs_realignment_) return;

  offset_ = CalculateTextAlignment(text_, size_, text_alignment_, *font_);

  text_needs_realignment_ = false;
}

void Label::AssignDefaultFontIfUnassigned() {
  if (font_ == nullptr) font_ = GetBook12UiFont();
}

}  // namespace components
}  // namespace ui
}  // namespace perception