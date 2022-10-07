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

#include "perception/ui/button.h"

#include <iostream>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "perception/draw.h"
#include "perception/font.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

using ::permebuf::perception::devices::MouseButton;

namespace perception {
namespace ui {

Button::Button() : is_pushed_down_(false) {}

Button::~Button() {}

Button* Button::OnClick(std::function<void()> on_click_handler) {
  on_click_handler_ = on_click_handler;
  return this;
}

void Button::OnMouseLeave() {
  if (is_pushed_down_) {
    is_pushed_down_ = false;
    InvalidateRender();
  }
}

void Button::OnMouseButtonDown(float x, float y, MouseButton button) {
  if (button != MouseButton::Left) return;

  if (is_pushed_down_) return;

  is_pushed_down_ = true;
  InvalidateRender();
}

void Button::OnMouseButtonUp(float x, float y, MouseButton button) {
  if (button != MouseButton::Left) return;

  if (!is_pushed_down_) return;

  is_pushed_down_ = false;
  InvalidateRender();

  if (on_click_handler_) on_click_handler_();
}

bool Button::GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
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

void Button::Draw(DrawContext& draw_context) {
  uint32 top_left_color;
  uint32 bottom_right_color;
  uint32 background_color;
  int text_offset;
  if (is_pushed_down_) {
    top_left_color = kButtonDarkColor;
    bottom_right_color = kButtonBrightColor;
    background_color = kButtonPushedBackgroundColor;
    text_offset = 1;
  } else {
    top_left_color = kButtonBrightColor;
    bottom_right_color = kButtonDarkColor;
    background_color = kButtonBackgroundColor;
    text_offset = 0;
  }

  int x = (int)(GetLeft() + draw_context.offset_x);
  int y = (int)(GetTop() + draw_context.offset_y);

  int width = (int)GetCalculatedWidth();
  int height = (int)GetCalculatedHeight();

  draw_context.skia_canvas->save();

  // Left line.
  SkPaint p;
  p.setColor(top_left_color);
  p.setStyle(SkPaint::kStroke_Style);
  p.setStrokeWidth(1);
  draw_context.skia_canvas->drawLine(x, y, x, y + height, p);

  // Top line.
  draw_context.skia_canvas->drawLine(x + 1, y, x + width - 1, y, p);

  // Right line.
  p.setColor(bottom_right_color);
  draw_context.skia_canvas->drawLine(x + width - 1, y + 1, x + width - 1, y + height - 1, p);

  // Bottom line.
  draw_context.skia_canvas->drawLine(x + 1, y + height - 1, x + width - 2, y + height - 1, p);

  // Draw background.
  draw_context.skia_canvas->clipIRect(
      SkIRect::MakeLTRB(x + 1, y + 1, x + width - 1, y + height - 1));

  draw_context.skia_canvas->drawColor(background_color);
  draw_context.skia_canvas->restore();

  Widget::Draw(draw_context);
}

}  // namespace ui
}  // namespace perception