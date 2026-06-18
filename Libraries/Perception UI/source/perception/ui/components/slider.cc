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

#include "perception/ui/components/slider.h"

#include <algorithm>
#include <cmath>

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/measurements.h"
#include "perception/ui/theme.h"

namespace perception {

template class UniqueIdentifiableType<ui::components::Slider>;

namespace ui {
namespace components {

Slider::Slider()
    : minimum_(0.0f), maximum_(1.0f), value_(0.0f), is_dragging_(false) {}

void Slider::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node.expired()) return;
  auto strong_node = node.lock();
  strong_node->SetBlocksHitTest(true);
  strong_node->SetCursor(window::Cursor::Poke);
  strong_node->OnDraw(std::bind_front(&Slider::Draw, this));
  strong_node->SetMeasureFunction(std::bind_front(&Slider::Measure, this));
  strong_node->OnMouseHover(std::bind_front(&Slider::MouseHover, this));
  strong_node->OnMouseLeave(std::bind_front(&Slider::MouseLeave, this));
  strong_node->OnMouseButtonDown(
      std::bind_front(&Slider::MouseButtonDown, this));
  strong_node->OnMouseButtonUp(std::bind_front(&Slider::MouseButtonUp, this));
}

void Slider::SetRange(float minimum, float maximum) {
  minimum_ = minimum;
  maximum_ = maximum;
  if (value_ < minimum_) value_ = minimum_;
  if (value_ > maximum_) value_ = maximum_;
  if (!node_.expired()) node_.lock()->Invalidate();
}

void Slider::SetValue(float value) {
  if (value < minimum_) value = minimum_;
  if (value > maximum_) value = maximum_;
  if (value_ == value) return;
  value_ = value;
  if (!node_.expired()) node_.lock()->Invalidate();
}

float Slider::GetValue() const { return value_; }

void Slider::OnChange(std::function<void(float)> on_change) {
  on_change_handlers_.push_back(on_change);
}

float Slider::CalculateValueFromX(float x, float width) {
  float clickable_width = width - (kSliderThumbRadius * 2.0f);
  if (clickable_width <= 0.0f) return minimum_;
  float offset_x = x - kSliderThumbRadius;
  float pct = offset_x / clickable_width;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 1.0f) pct = 1.0f;
  return minimum_ + pct * (maximum_ - minimum_);
}

void Slider::MouseHover(const Point& point) {
  if (node_.expired()) return;
  auto strong_node = node_.lock();
  if (is_dragging_) {
    float new_value =
        CalculateValueFromX(point.x, strong_node->GetSize().width);
    if (new_value != value_) {
      value_ = new_value;
      for (const auto& handler : on_change_handlers_) {
        handler(value_);
      }
      strong_node->Invalidate();
    }
  }
}

void Slider::MouseLeave() {
  if (is_dragging_) {
    is_dragging_ = false;
    if (!node_.expired()) node_.lock()->Invalidate();
  }
}

void Slider::MouseButtonDown(const Point& point, window::MouseButton button) {
  if (button != window::MouseButton::Left || is_dragging_ || node_.expired())
    return;
  is_dragging_ = true;
  auto strong_node = node_.lock();
  float new_value = CalculateValueFromX(point.x, strong_node->GetSize().width);
  if (new_value != value_) {
    value_ = new_value;
    for (const auto& handler : on_change_handlers_) {
      handler(value_);
    }
  }
  strong_node->Invalidate();
}

void Slider::MouseButtonUp(const Point& point, window::MouseButton button) {
  if (button != window::MouseButton::Left || !is_dragging_) return;
  is_dragging_ = false;
  if (!node_.expired()) node_.lock()->Invalidate();
}

void Slider::Draw(const DrawContext& draw_context) {
  if (draw_context.area.size.width <= 0.0f ||
      draw_context.area.size.height <= 0.0f)
    return;

  SkPaint paint;
  paint.setAntiAlias(true);

  // Draw track
  float track_y =
      draw_context.area.origin.y + (draw_context.area.size.height / 2.0f);
  paint.setColor(kSliderTrackColor);
  paint.setStyle(SkPaint::kFill_Style);
  draw_context.skia_canvas->drawRect(
      {draw_context.area.origin.x + kSliderThumbRadius,
       track_y - (kSliderTrackThickness / 2.0f),
       draw_context.area.MaxX() - kSliderThumbRadius,
       track_y + (kSliderTrackThickness / 2.0f)},
      paint);

  // Draw thumb
  float clickable_width =
      draw_context.area.size.width - (kSliderThumbRadius * 2.0f);
  float pct = 0.0f;
  if (maximum_ > minimum_) {
    pct = (value_ - minimum_) / (maximum_ - minimum_);
  }
  float thumb_x =
      draw_context.area.origin.x + kSliderThumbRadius + (pct * clickable_width);

  paint.setColor(is_dragging_ ? kSliderThumbHoverColor : kSliderThumbColor);
  draw_context.skia_canvas->drawCircle(thumb_x, track_y, kSliderThumbRadius,
                                       paint);
}

Size Slider::Measure(float width, YGMeasureMode width_mode, float height,
                     YGMeasureMode height_mode) {
  return {
      .width = CalculateMeasuredLength(width_mode, width, kSliderMinWidth),
      .height = CalculateMeasuredLength(height_mode, height, kSliderHeight)};
}

}  // namespace components
}  // namespace ui
}  // namespace perception
