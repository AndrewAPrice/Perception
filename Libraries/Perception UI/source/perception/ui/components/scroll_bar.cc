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
#include "perception/ui/components/scroll_bar.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/measurements.h"
#include "perception/ui/node.h"
#include "perception/ui/rectangle.h"
#include "types.h"

using Direction = perception::ui::components::ScrollBar::Direction;

namespace perception {
namespace ui {
namespace components {
namespace {

constexpr float kMinFabSize = 12;
constexpr uint32 kIdleFabColor = SkColorSetARGB(0xff, 0xDC, 0xDC, 0xDC);
constexpr uint32 kHoverFabColor = SkColorSetARGB(0xff, 0xCF, 0xCF, 0XCF);
constexpr uint32 kDragFabColor = SkColorSetARGB(0xff, 0xC0, 0xC0, 0XC0);

}  // namespace

ScrollBar::ScrollBar()
    : direction_(Direction::HORIZONTAL), always_show_(true) {}

void ScrollBar::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node.expired()) return;
  auto strong_node = node.lock();
  strong_node->SetBlocksHitTest(true);
  strong_node->OnDraw(std::bind_front(&ScrollBar::Draw, this));
  strong_node->SetMeasureFunction(std::bind_front(&ScrollBar::Measure, this));
  strong_node->OnMouseHover(std::bind_front(&ScrollBar::MouseHover, this));
  strong_node->OnMouseLeave(std::bind_front(&ScrollBar::MouseLeave, this));
  strong_node->OnMouseButtonDown(
      std::bind_front(&ScrollBar::MouseButtonDown, this));
  strong_node->OnMouseButtonUp(
      std::bind_front(&ScrollBar::MouseButtonUp, this));
}

std::shared_ptr<Node> ScrollBar::GetFab() { return fab_; }

void ScrollBar::SetDirection(Direction direction) {
  if (direction == direction_) return;
  direction_ = direction;

  if (node_.expired()) return;
  node_.lock()->Invalidate();
}

Direction ScrollBar::GetDirection() const { return direction_; }

void ScrollBar::SetAlwaysShowScrollBar(bool always_show) {
  if (always_show_ == always_show) return;
  always_show_ = always_show;

  if (!is_mouse_hovering_over_track_ && !node_.expired())
    node_.lock()->Invalidate();
}

bool ScrollBar::AlwaysShowScrollBar() const { return always_show_; }

void ScrollBar::OnScroll(std::function<void(float)> on_scroll_handler) {
  on_scroll_handlers_.push_back(on_scroll_handler);
}

void ScrollBar::SetValue(float minimum, float maximum, float value,
                         float size) {
  if (minimum_ == minimum && maximum_ == maximum && value_ == value &&
      size_ == size) {
    return;
  }

  minimum_ = minimum;
  maximum_ = maximum;
  value_ = value;
  size_ = size;
}

float ScrollBar::GetValue() const { return value_; }

std::pair<float, float> ScrollBar::CalculateFabOffsetAndSize(
    float available_length) const {
  float range = maximum_ - minimum_;
  if (range == 0.0f) {
    return {0.0f, available_length};
  }
  // Percentage of the scroll bar that the fab takes up.
  float size = available_length / range;
  // Percentage of the scroll bar to be offset into.
  float offset = (value_ - minimum_) / range;

  return {offset * available_length, size * available_length};
}

float ScrollBar::CalculateDragPosition(float mouse_offset, float fab_length,
                                       float track_length) {
  float track_clickable_length = track_length - fab_length;
  if (track_clickable_length <= 0) return 0.0f;

  float fab_offset = fab_length / 2.0f;

  if (is_mouse_hovering_over_fab_) {
    mouse_offset += fab_length / 2.0f - fab_drag_offset_;
  }

  // Get percentage along the clickable length.
  float clicked_track_pct =
      (mouse_offset - fab_offset) / track_clickable_length;
  clicked_track_pct = std::max(std::min(clicked_track_pct, 1.0f), 0.f);
  return minimum_ + (clicked_track_pct * (maximum_ - minimum_));
}

void ScrollBar::AdjustRectangleForFab(Rectangle& rectangle) const {
  switch (direction_) {
    case Direction::HORIZONTAL: {
      auto [offset, length] = CalculateFabOffsetAndSize(rectangle.size.width);
      rectangle.origin.x += offset;
      rectangle.size.width = length;
    } break;
    case Direction::VERTICAL: {
      auto [offset, length] = CalculateFabOffsetAndSize(rectangle.size.height);
      rectangle.origin.y += offset;
      rectangle.size.height = length;
    } break;
  }
}

Rectangle ScrollBar::GetFabArea() const {
  if (node_.expired()) return {};
  auto strong_node = node_.lock();
  Layout layout = strong_node->GetLayout();

  Rectangle rectangle = {.origin = {.x = 0.0f, .y = 0.0f},
                         .size{.width = layout.GetCalculatedWidth(),
                               .height = layout.GetCalculatedHeight()}};
  AdjustRectangleForFab(rectangle);
  return rectangle;
}

uint32 ScrollBar::GetFabColor() const {
  if (is_dragging_) return kDragFabColor;
  if (is_mouse_hovering_over_fab_) return kHoverFabColor;
  if (always_show_ || is_mouse_hovering_over_track_) return kIdleFabColor;
  return 0;
}

void ScrollBar::MouseHover(const Point& point) {
  if (node_.expired()) return;
  uint32 previous_fab_color = GetFabColor();
  Rectangle fab_area = GetFabArea();

  is_mouse_hovering_over_track_ = true;

  if (is_dragging_) {
    // Move the fab here.
    float value = value_;

    auto strong_node = node_.lock();
    Layout layout = strong_node->GetLayout();

    switch (direction_) {
      case Direction::HORIZONTAL:
        value = CalculateDragPosition(point.x, fab_area.size.width,
                                      layout.GetCalculatedWidth());
        break;
      case Direction::VERTICAL:
        value = CalculateDragPosition(point.y, fab_area.size.height,
                                      layout.GetCalculatedHeight());
        break;
    }
    if (value != value_) {
      value_ = value;
      for (const auto& handler : on_scroll_handlers_) handler(value);
      strong_node->Invalidate();
    }
    is_mouse_hovering_over_fab_ = true;
  } else {
    is_mouse_hovering_over_fab_ = fab_area.Contains(point);
  }

  if (previous_fab_color != GetFabColor() && !node_.expired())
    node_.lock()->Invalidate();
}

void ScrollBar::MouseLeave() {
  uint32 previous_fab_color = GetFabColor();

  is_mouse_hovering_over_track_ = false;
  is_mouse_hovering_over_fab_ = false;
  is_dragging_ = false;

  if (previous_fab_color != GetFabColor() && !node_.expired())
    node_.lock()->Invalidate();
}

void ScrollBar::MouseButtonDown(const Point& point,
                                window::MouseButton button) {
  if (button != window::MouseButton::Left || is_dragging_ || node_.expired())
    return;

  uint32 previous_fab_color = GetFabColor();
  if (is_mouse_hovering_over_fab_) {
    // The user started to draw the fab. Record the grab position but don't move
    // the fab until the user moves the mouse.
    is_dragging_ = true;

    auto strong_node = node_.lock();
    auto layout = strong_node->GetLayout();

    switch (direction_) {
      case Direction::HORIZONTAL: {
        auto [offset, _] =
            CalculateFabOffsetAndSize(layout.GetCalculatedWidth());
        fab_drag_offset_ = point.x - offset;
        break;
      }
      case Direction::VERTICAL: {
        auto [offset, _] =
            CalculateFabOffsetAndSize(layout.GetCalculatedHeight());
        fab_drag_offset_ = point.y - offset;
        break;
      }
      default:
        fab_drag_offset_ = 0.0f;
        break;
    }

  } else if (is_mouse_hovering_over_track_) {
    // User clicked the track, so move the fab to the track location.
    is_dragging_ = true;
    MouseHover(point);
  }

  if (previous_fab_color != GetFabColor() && !node_.expired())
    node_.lock()->Invalidate();
}

void ScrollBar::MouseButtonUp(const Point& point, window::MouseButton button) {
  if (button != window::MouseButton::Left) return;
  if (!is_dragging_) return;
  uint32 previous_fab_color = GetFabColor();
  is_dragging_ = false;

  if (previous_fab_color != GetFabColor() && !node_.expired())
    node_.lock()->Invalidate();
}

void ScrollBar::Draw(const DrawContext& draw_context) {
  uint32 fab_color = GetFabColor();
  if (fab_color == 0) return;

  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(GetFabColor());
  p.setStyle(SkPaint::kFill_Style);

  Rectangle rectangle = draw_context.area;
  AdjustRectangleForFab(rectangle);
  draw_context.skia_canvas->drawRect({rectangle.origin.x, rectangle.origin.y,
                                      rectangle.MaxX(), rectangle.MaxY()},
                                     p);
}

Size ScrollBar::Measure(float width, YGMeasureMode width_mode, float height,
                        YGMeasureMode height_mode) {
  switch (direction_) {
    case Direction::HORIZONTAL:
      return {
          .width = CalculateMeasuredLength(width_mode, width, 0.0f),
          .height = CalculateMeasuredLength(height_mode, height, kMinFabSize)};
    case Direction::VERTICAL:
      return {.width = CalculateMeasuredLength(width_mode, width, kMinFabSize),
              .height = CalculateMeasuredLength(height_mode, height, 0.0f)};
    default:
      return {.width = CalculateMeasuredLength(width_mode, width, 0.0f),
              .height = CalculateMeasuredLength(height_mode, height, 0.0f)};
  }
}

}  // namespace components
}  // namespace ui
}  // namespace perception