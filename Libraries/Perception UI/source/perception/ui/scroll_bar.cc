// Copyright 2022 Google LLC
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

#include "perception/ui/scroll_bar.h"

#include <iostream>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

using ::permebuf::perception::devices::MouseButton;

namespace perception {
namespace ui {

std::shared_ptr<ScrollBar> ScrollBar::Create() {
  return std::shared_ptr<ScrollBar>(new ScrollBar());
}

ScrollBar::~ScrollBar() {}

ScrollBar* ScrollBar::SetDirection(ScrollBar::Direction direction) {
  if (direction_ == direction) return this;

  SetDirectionInternal(direction);
  InvalidateRender();

  return this;
}

ScrollBar::Direction ScrollBar::GetDirection() const { return direction_; }

ScrollBar* ScrollBar::OnScroll(std::function<void(float)> on_scroll_handler) {
  on_scroll_handler_ = on_scroll_handler;
  return this;
}

ScrollBar* ScrollBar::SetPosition(float minimum, float maximum, float value,
                                  float size) {
  if (minimum > maximum) std::swap(minimum, maximum);

  if (maximum == minimum) {
    // Should never happen but create a scrollbar that doesn't cause a divide by
    // zero.
    minimum = 0.0f;
    maximum = 1.0f;
    value = 0.0f;
    size = 1.0f;
  }

  value = std::min(std::max(value, minimum), maximum - size);

  if (minimum_ == minimum && maximum_ == maximum && value_ == value &&
      size_ == size)
    return this;

  minimum_ = minimum;
  maximum_ = maximum;
  int old_value = value_;
  value_ = value;
  size_ = size;

  if (value != old_value && on_scroll_handler_) on_scroll_handler_(value);

  InvalidateRender();
  return this;
}

float ScrollBar::GetPosition() const { return value_; }

bool ScrollBar::GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                            float& x_in_selected_widget,
                            float& y_in_selected_widget) {
  float left = GetLeft();
  float top = GetTop();
  float right = left + GetCalculatedWidth();
  float bottom = top + GetCalculatedHeight();

  if (x < left || y < top || x >= right || y >= bottom) {
    // Out of bounds.
    return false;
  }

  widget = ToSharedPtr();
  x_in_selected_widget = x - GetLeft();
  y_in_selected_widget = y - GetTop();
  return true;
}

void ScrollBar::OnMouseEnter() {}

void ScrollBar::OnMouseMove(float x, float y) {
  bool is_mouse_hovering_over_track = false;
  bool is_mouse_hovering_over_fab = false;

  float track_x = GetLeft();
  float track_y = GetTop();
  float track_width = GetCalculatedWidth();
  float track_height = GetCalculatedHeight();

  float fab_x = track_x;
  float fab_y = track_y;
  float fab_width = track_width;
  float fab_height = track_height;
  OffsetTrackToFabCoordinates(fab_x, fab_y, fab_width, fab_height);

  if (is_dragging_) {
    // Move the fab here.
    float value = value_;
    if (direction_ == ScrollBar::Direction::HORIZONTAL) {
      // The fab can't go past the end of the track, the
      // clickable width is start+half_fab to end-half_fab.
      float track_clickable_width = track_width - fab_width;
      if (track_clickable_width > 0) {
        track_x += fab_width / 2.0f;

        float mouse_x = x;

        if (is_mouse_hovering_over_fab_) {
          mouse_x += fab_width / 2.0f - fab_drag_offset_;
        }

        // Get percentage along the clickable width.
        float clicked_track_pct = (mouse_x - track_x) / track_clickable_width;
        clicked_track_pct = std::max(std::min(clicked_track_pct, 1.0f), 0.0f);

        value = minimum_ + (clicked_track_pct * (maximum_ - minimum_ - size_));
      }
    } else {
      // The fab can't go past the end of the track, the
      // clickable width is start+half_fab to end-half_fab.
      float track_clickable_height = track_height - fab_height;
      if (track_clickable_height > 0) {
        track_y += fab_height / 2.0f;

        float mouse_y = y;

        if (is_mouse_hovering_over_fab_) {
          mouse_y += fab_height / 2.0f - fab_drag_offset_;
        }

        // Get percentage along the clickable height.
        float clicked_track_pct = (mouse_y - track_y) / track_clickable_height;
        clicked_track_pct = std::max(std::min(clicked_track_pct, 1.0f), 0.0f);

        value = minimum_ + (clicked_track_pct * (maximum_ - minimum_ - size_));
      }
    }

    if (value != value_) {
      value_ = value;
      if (on_scroll_handler_) on_scroll_handler_(value);
    }

    if (is_mouse_hovering_over_track_) {
      // Now click again, so the user grips onto the fab.
      is_dragging_ = false;
      OnMouseButtonDown(x, y, MouseButton::Left);
    } else {
      InvalidateRender();
    }
  } else {
    if (x >= fab_x && y >= fab_y && x < fab_x + fab_width &&
        y < fab_y + fab_height) {
      is_mouse_hovering_over_fab = true;
    } else if (x >= track_x && y >= track_y && x < track_x + track_width &&
               y < track_y + track_height) {
      is_mouse_hovering_over_track = true;
    }

    if (is_mouse_hovering_over_fab != is_mouse_hovering_over_fab_ ||
        is_mouse_hovering_over_track != is_mouse_hovering_over_track_) {
      is_mouse_hovering_over_fab_ = is_mouse_hovering_over_fab;
      is_mouse_hovering_over_track_ = is_mouse_hovering_over_track;
      InvalidateRender();
    }
  }
}

void ScrollBar::OnMouseLeave() {
  bool invalidate =
      is_mouse_hovering_over_track_ || is_mouse_hovering_over_fab_;
  is_mouse_hovering_over_track_ = false;
  is_mouse_hovering_over_fab_ = false;
  is_dragging_ = false;

  if (invalidate) InvalidateRender();
}

void ScrollBar::OnMouseButtonDown(float x, float y, MouseButton button) {
  if (button != MouseButton::Left || is_dragging_) return;

  OnMouseMove(x, y);

  if (is_mouse_hovering_over_fab_) {
    // User started to drag the fab. We'll need to record the grab offset,
    // but we won't move the fab until the user moves the mouse cursor.
    is_dragging_ = true;

    // Find where on the fab the user started dragging.
    float fab_x = GetLeft();
    float fab_y = GetTop();
    float width = GetCalculatedWidth();
    float height = GetCalculatedHeight();
    OffsetTrackToFabCoordinates(fab_x, fab_y, width, height);

    if (direction_ == ScrollBar::Direction::VERTICAL) {
      fab_drag_offset_ = y - fab_y;
    } else {
      fab_drag_offset_ = x - fab_x;
    }
  } else if (is_mouse_hovering_over_track_) {
    is_dragging_ = true;
    // User clicked the track, so move the fab to the track location.
    OnMouseMove(x, y);
  }
}

void ScrollBar::OnMouseButtonUp(float x, float y, MouseButton button) {
  if (button != MouseButton::Left) return;
  is_dragging_ = false;
}

ScrollBar::ScrollBar()
    : is_mouse_hovering_over_track_(false),
      is_mouse_hovering_over_fab_(false),
      is_dragging_(false),
      minimum_(0.0f),
      maximum_(1.0f),
      value_(0.0f),
      size_(0.0f) {
  SetDirectionInternal(Direction::VERTICAL);
}

void ScrollBar::SetDirectionInternal(Direction direction) {
  direction_ = direction;
  if (direction == ScrollBar::Direction::VERTICAL) {
    SetWidth(kScrollBarThickness);
    SetHeightPercent(100.0f);
  } else {
    SetWidthPercent(100.0f);
    SetHeight(kScrollBarThickness);
  }
}

void ScrollBar::Draw(DrawContext& draw_context) {
  uint32 track_background_color = is_mouse_hovering_over_track_
                                      ? kScrollBarTrackBackgroundHoverColor
                                      : kScrollBarTrackBackgroundColor;
  uint32 track_outline_color = kScrollBarTrackOutlineColor;
  uint32 fab_background_color = is_mouse_hovering_over_fab_
                                    ? kScrollBarFabBackgroundHoverColor
                                    : kScrollBarFabBackgroundColor;
  uint32 fab_outline_color = kScrollBarFabOutlineColor;

  float x = GetLeft() + draw_context.offset_x;
  float y = GetTop() + draw_context.offset_y;

  float width = GetCalculatedWidth();
  float height = GetCalculatedHeight();

  draw_context.skia_canvas->save();
  SkPaint p;
  p.setAntiAlias(true);

  // Draw track background.
  p.setColor(track_background_color);
  p.setStyle(SkPaint::kFill_Style);
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kScrollBarBorderRadius,
                                          kScrollBarBorderRadius, p);

  // Draw the track outline.
  p.setColor(track_outline_color);
  p.setStyle(SkPaint::kStroke_Style);
  p.setStrokeWidth(1);
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kButtonCornerRadius,
                                          kButtonCornerRadius, p);
  OffsetTrackToFabCoordinates(x, y, width, height);

  // Draw the fab background.
  p.setColor(fab_background_color);
  p.setStyle(SkPaint::kFill_Style);
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kScrollBarBorderRadius,
                                          kScrollBarBorderRadius, p);

  // Draw the fab outline.
  p.setColor(fab_outline_color);
  p.setStyle(SkPaint::kStroke_Style);
  p.setStrokeWidth(1);
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kButtonCornerRadius,
                                          kButtonCornerRadius, p);

  draw_context.skia_canvas->restore();

  // Draw the contents of the button.
  Widget::Draw(draw_context);
}

void ScrollBar::OffsetTrackToFabCoordinates(float& x, float& y, float& width,
                                            float& height) {
  float range = maximum_ - minimum_;
  // Percentage of the scroll bar that the fab takes up.
  float size = size_ / range;
  // Percentage of the scorll bar we're offet into
  float offset = (value_ - minimum_) / range;

  if (direction_ == ScrollBar::Direction::VERTICAL) {
    y += offset * height;
    height *= size;
  } else {
    x += offset * width;
    width *= size;
  }
}

}  // namespace ui
}  // namespace perception